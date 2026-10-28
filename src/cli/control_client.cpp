// SPDX-License-Identifier: GPL-3.0-or-later
/* A request to the running compositor's control socket, as `paw msg` and the other
 * subcommands send it, and the rows of its reply. */
#include "paw/cli.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace paw::cli {

Reply request(const std::string &line) {
    if (line.empty() || line.find('\n') != std::string::npos)
        throw std::runtime_error("a request is one line");
    const char *path = std::getenv("PAW_SOCKET");
    if (!path || !*path)
        throw SessionUnreachable("PAW_SOCKET is not set; run inside a paw session");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof(address.sun_path))
        throw std::runtime_error("control socket path is too long");
    std::strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        throw std::runtime_error(std::string("cannot make a socket: ") + std::strerror(errno));
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        int error = errno;
        close(fd);
        throw SessionUnreachable(std::string("cannot connect to ") + path + ": " +
                                 std::strerror(error));
    }
    std::string message = line + '\n';
    for (size_t sent = 0; sent < message.size();) {
        ssize_t written = write(fd, message.data() + sent, message.size() - sent);
        if (written <= 0) {
            close(fd);
            throw std::runtime_error("cannot send request");
        }
        sent += static_cast<size_t>(written);
    }
    std::string reply;
    char buffer[4096];
    for (ssize_t count; (count = read(fd, buffer, sizeof(buffer))) > 0;)
        reply.append(buffer, static_cast<size_t>(count));
    close(fd);
    if (reply.empty())
        return {false, "no reply\n"};
    bool ok = reply.rfind("ok", 0) == 0;
    // An error is "error\nWHY" or "error: WHY" on one line; keep the reason either way.
    if (!ok)
        return {false, reply};
    return {true, reply.substr(std::min(reply.find('\n') + 1, reply.size()))};
}

std::vector<std::vector<std::string>> rows(const std::string &body) {
    std::vector<std::vector<std::string>> result;
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find('\n', start);
        if (end == std::string::npos)
            end = body.size();
        std::vector<std::string> fields;
        for (size_t field = start;;) {
            size_t tab = body.find('\t', field);
            if (tab == std::string::npos || tab > end) {
                fields.push_back(body.substr(field, end - field));
                break;
            }
            fields.push_back(body.substr(field, tab - field));
            field = tab + 1;
        }
        result.push_back(std::move(fields));
        start = end + 1;
    }
    return result;
}

std::vector<std::vector<std::string>> query(const std::string &name) {
    auto reply = request("get " + name);
    if (!reply.ok) {
        auto why = reply.body;
        while (!why.empty() && why.back() == '\n')
            why.pop_back();
        throw std::runtime_error("get " + name + ": " + why);
    }
    return rows(reply.body);
}

std::filesystem::path state_directory() {
    if (const auto *xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg == '/')
        return std::filesystem::path(xdg) / "paw";
    if (const auto *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local/state/paw";
    return {};
}

} // namespace paw::cli
