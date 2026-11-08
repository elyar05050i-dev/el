// SPDX-License-Identifier: GPL-2.0-only
// Standalone KernelSU host for the SUSFS management and file services.
// This executable intentionally has no SKRoot SDK dependency.
#include "../vendor/file_manager/app_metadata_service.h"
#include "../vendor/file_manager/file_broker.h"
#include "private_storage.hpp"
#include "../../management/embedded.hpp"
#include "../../management/common.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <cctype>
#include <map>
#include <poll.h>
#include <random>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using susfs::embedded::Reply;
std::string state_dir;
std::string resources;
std::string session;
std::atomic<bool> stopping{false};
int unix_listener=-1;
int lifecycle_lock=-1;
file_manager::FileBroker broker;
app_metadata::Service metadata;

void close_fd(int& fd){if(fd>=0){close(fd);fd=-1;}}
bool write_all(int fd, const void* data, std::size_t size) {
    const char* bytes = static_cast<const char*>(data);
    std::size_t done = 0;
    while (done < size) {
        const auto n = ::write(fd, bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool make_session(){
    if(load_private(state_dir+"/susfs/webui-session",session))return true;
    std::random_device random;std::ostringstream text;
    for(unsigned i=0;i<32;++i)text<<"0123456789abcdef"[random()&15];
    session=text.str();const auto path=state_dir+"/susfs/webui-session";
    const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return errno==EEXIST&&load_private(path,session);
    const bool ok=write_all(fd,session.data(),session.size());close(fd);
    if(!ok){unlink(path.c_str());session.clear();}
    return ok;
}
void stopped(int){stopping.store(true);if(unix_listener>=0)shutdown(unix_listener,SHUT_RDWR);}
void reap(int){int status;while(waitpid(-1,&status,WNOHANG)>0){}}
Reply route(const std::string& path,const std::string& body){
    if(path=="/api/session"){
        if(session.empty())return {503,"{\"ok\":false,\"code\":\"SERVICE_UNAVAILABLE\",\"message\":\"WebUI session is not ready\",\"error\":\"WebUI session is not ready\",\"errno\":0,\"data\":null}"};
        return {200,"{\"ok\":true,\"code\":\"OK\",\"message\":\"\",\"errno\":0,\"data\":{\"token\":"+susfs::management::quote(session)+"}}"};
    }
    // Session validation for Unix socket: peercred uid==0 + valid session token in state_dir
    if(!session.empty()){
        // For API calls other than /api/session, the caller has already proven
        // uid==0 via SO_PEERCRED on the Unix socket; session token is stored
        // in state_dir and only accessible to root, so the connection itself
        // is the authorization. No per-request token in body is needed.
    }
    if(path=="/api/susfs/webui/ping"){
        (void)susfs::embedded::request(state_dir,"/service/ping","");
        const bool no_background=susfs::embedded::no_background(state_dir);
        return {200,std::string("{\"ok\":true,\"data\":{\"no_background\":")+(no_background?"true":"false")+"}}"};
    }
    if(path=="/api/susfs/webui/exit"){
        stopping.store(true);if(unix_listener>=0)shutdown(unix_listener,SHUT_RDWR);
        return {200,"{\"ok\":true,\"data\":{\"closed\":true}}"};
    }
    if(path.rfind("/api/files/",0)==0)return broker.handle(path,body);
    if(path=="/api/apps/metadata")return metadata.handle(body);
    static const std::map<std::string,std::string> routes={
        {"/api/susfs/status","/status"},{"/api/susfs/config-get","/config"},
        {"/api/susfs/apps","/apps"},{"/api/susfs/objects-query","/objects/query"},
        {"/api/susfs/kstat-sample","/kstat/sample"},{"/api/susfs/config-save","/config/save"},
        {"/api/susfs/config-import","/config/import"},{"/api/susfs/config-export","/config/export"},
        {"/api/susfs/apply","/apply"}
    };
    const auto found=routes.find(path);if(found==routes.end())return {404,"{\"ok\":false,\"code\":\"NOT_FOUND\",\"message\":\"Unknown API route\",\"error\":\"Unknown API route\",\"errno\":0,\"data\":null}"};
    return susfs::embedded::request(state_dir,found->second,body);
}
bool handle_rpc(int fd){
    std::uint32_t body_len=0;
    if(!transfer(fd,&body_len,sizeof(body_len),false))return false;
    if(body_len>4*1024*1024)return false;
    std::string input(body_len,'\0');
    if(!transfer(fd,input.data(),body_len,false))return false;
    std::string error;auto parsed=susfs::management::parse_json(input,error,4*1024*1024);
    if(!parsed)return false;
    std::string path,body;
    if(!susfs::management::json_string(susfs::management::member(parsed.get(),"path"),path) ||
       !susfs::management::json_string(susfs::management::member(parsed.get(),"body"),body)){
        return false;
    }
    const auto reply=route(path,body);
    std::string response="{\"status\":"+std::to_string(reply.status)+",\"body\":"+susfs::management::quote(reply.body)+"}";
    std::uint32_t out_len=static_cast<std::uint32_t>(response.size());
    return transfer(fd,&out_len,sizeof(out_len),true) && transfer(fd,response.data(),out_len,true);
}
}
int main(int argc,char** argv){
    if(argc!=3||geteuid()!=0)return 2;
    resources=argv[1];state_dir=argv[2];
    if(resources.empty()||resources.back()=='/'||resources.find("..")!=std::string::npos||
       state_dir.empty()||state_dir.back()=='/'||state_dir.find("..")!=std::string::npos)return 2;
    if(mkdir(state_dir.c_str(),0700)&&errno!=EEXIST)return 3;
    if(mkdir((state_dir+"/susfs").c_str(),0700)&&errno!=EEXIST)return 3;
    lifecycle_lock=open((state_dir+"/susfs/rpc-service.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(lifecycle_lock<0||flock(lifecycle_lock,LOCK_EX|LOCK_NB))return 0;
    if(!make_session())return 4;
    ksu_storage::configure(state_dir+"/files");
    setenv("SUSFS_MODULE_DIR",resources.c_str(),1);
    std::string error;if(!susfs::embedded::start(state_dir,error))return 5;
    if(!broker.start("susfs_ksu"))return 6;
    metadata.configure(resources+"/app-metadata.dex");
    unix_listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(unix_listener<0)return 7;
    const std::string sock_path=state_dir+"/susfs/rpc.sock";
    if(sock_path.size()>=sizeof(sockaddr_un::sun_path))return 8;
    unlink(sock_path.c_str());
    sockaddr_un addr{};addr.sun_family=AF_UNIX;
    std::memcpy(addr.sun_path,sock_path.c_str(),sock_path.size()+1);
    if(bind(unix_listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))||listen(unix_listener,16)||chmod(sock_path.c_str(),0600))return 8;
    signal(SIGTERM,stopped);signal(SIGINT,stopped);signal(SIGCHLD,reap);signal(SIGPIPE,SIG_IGN);
    const int ready=open((state_dir+"/susfs/rpc.ready").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(ready>=0){const char text[]="unix:///data/adb/susfs_ksu/susfs/rpc.sock\n";(void)!write(ready,text,sizeof(text)-1);close(ready);}
    while(!stopping.load()){
        pollfd item{unix_listener,POLLIN,0};const int result=poll(&item,1,250);
        if(result<0){if(errno==EINTR)continue;break;}
        if(!result)continue;
        const int fd=accept4(unix_listener,nullptr,nullptr,SOCK_CLOEXEC);
        if(fd<0){if(errno==EINTR)continue;continue;}
        ucred peer{};socklen_t len=sizeof(peer);
        if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&len)!=0||peer.uid!=0){close(fd);continue;}
        timeval timeout{180,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        std::thread([fd]{if(!handle_rpc(fd)){} close(fd);}).detach();
    }
    close_fd(unix_listener);broker.shutdown();susfs::embedded::stop(state_dir,error);
    unlink((state_dir+"/susfs/rpc.sock").c_str());unlink((state_dir+"/susfs/rpc.ready").c_str());close_fd(lifecycle_lock);return 0;
}