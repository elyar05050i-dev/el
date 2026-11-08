// SPDX-License-Identifier: GPL-2.0-only
// RPC client for the susfs_ksu_daemon Unix socket.
// Usage: susfs_ksu_rpc <socket_path> [base64_request_json]
// If no argument provided, reads request JSON from stdin (binary, not base64).
// Outputs raw response JSON to stdout.
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <cctype>

static bool transfer(int fd, void* data, std::size_t size, bool sending) {
    auto* bytes = static_cast<char*>(data);
    std::size_t done = 0;
    while (done < size) {
        const auto n = sending ? ::send(fd, bytes + done, size - done, MSG_NOSIGNAL)
                               : ::recv(fd, bytes + done, size - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

static std::string base64_decode(const std::string& in) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> decode(256, -1);
    for (int i = 0; i < 64; ++i) decode[static_cast<unsigned char>(table[i])] = i;

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        if (decode[c] == -1) continue;
        val = (val << 6) + decode[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <socket_path> [base64_request_json]\n";
        return 1;
    }
    const std::string sock_path = argv[1];
    std::string request_json;

    if (argc == 3) {
        request_json = base64_decode(argv[2]);
    } else {
        std::istreambuf_iterator<char> it(std::cin), end;
        request_json.assign(it, end);
    }

    // Validate request JSON is valid UTF-8 and parseable
    // We don't parse here; just pass through. Daemon will validate.

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 1;

    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    if (sock_path.size() >= sizeof(addr.sun_path)) {
        close(fd); return 1;
    }
    std::memcpy(addr.sun_path, sock_path.c_str(), sock_path.size() + 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd); return 1;
    }

    // Send request length + body
    const std::uint32_t len = static_cast<std::uint32_t>(request_json.size());
    if (!transfer(fd, &len, sizeof(len), true) ||
        !transfer(fd, const_cast<char*>(request_json.data()), len, true)) {
        close(fd); return 1;
    }

    // Read response length + body
    std::uint32_t resp_len = 0;
    if (!transfer(fd, &resp_len, sizeof(resp_len), false) || resp_len > 16 * 1024 * 1024) {
        close(fd); return 1;
    }
    std::string response(resp_len, '\0');
    if (!transfer(fd, response.data(), resp_len, false)) {
        close(fd); return 1;
    }
    close(fd);

    // Write raw response to stdout
    std::cout.write(response.data(), response.size());
    return 0;
}