// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace ksu_storage {
inline std::string directory = "/data/adb/susfs_ksu/files";
inline void configure(const std::string& path) { directory = path; }
inline bool read(const std::string& key, std::string& output) {
    output.clear();
    const int fd = open((directory + "/" + key + ".json").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    char buffer[8192];
    for (;;) {
        const auto count = ::read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return count == 0; }
        output.append(buffer, static_cast<std::size_t>(count));
        if (output.size() > 1048576) { close(fd); output.clear(); return false; }
    }
}
inline bool write(const std::string& key, const std::string& value, std::string& error) {
    if (mkdir(directory.c_str(), 0700) && errno != EEXIST) { error = std::strerror(errno); return false; }
    const auto target = directory + "/" + key + ".json", temporary = target + ".tmp";
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { error = std::strerror(errno); return false; }
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto count = ::write(fd, value.data() + offset, value.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { error = std::strerror(errno); close(fd); unlink(temporary.c_str()); return false; }
        offset += static_cast<std::size_t>(count);
    }
    if (fsync(fd) || close(fd) || rename(temporary.c_str(), target.c_str())) {
        error = std::strerror(errno); unlink(temporary.c_str()); return false;
    }
    return true;
}
}
