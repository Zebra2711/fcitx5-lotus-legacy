/*
 * SPDX-FileCopyrightText: 2026 LotusInputMethod contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-server-socket.h"

#include "lotus-logger.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

    constexpr int kListenBacklog = 5;

    /**
     * @brief Checks whether a live server is listening on the socket path.
     *
     * A crashed server leaves the socket file behind but connect() then fails
     * with ECONNREFUSED; that is the only case where the stale file is removed.
     */
    bool hasLiveServer(const UnixSocket& address) {
        if (address.path.empty() || access(address.path.c_str(), F_OK) != 0) {
            return false;
        }

        const int probe = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (probe < 0) {
            return true; // Do not delete a path we cannot probe.
        }

        const bool live = connect(probe, address.asSockaddr(), address.length) == 0;
        const int  err  = errno;
        close(probe);

        if (live) {
            return true;
        }
        if (err == ECONNREFUSED || err == ENOENT) {
            return false;
        }
        return true; // EACCES or an unexpected error: keep the file.
    }

} // namespace

bool bindServerSocket(int fd, UnixSocket& address, std::string_view label) {
    if (hasLiveServer(address)) {
        LotusLogger::instance().error("Another server is already listening on " + std::string(label) + ": " + address.path);
        return false;
    }

    if (!address.path.empty() && unlink(address.path.c_str()) != 0 && errno != ENOENT) {
        LotusLogger::instance().error("Failed to remove stale " + std::string(label) + " " + address.path + ": " + std::string(strerror(errno)));
        return false;
    }

    // Create the socket with owner-only permissions from the first instant,
    // then verify the mode with chmod below. The socket lives in a user-only
    // runtime directory, so a pathname socket plus SO_PEERCRED keeps other
    // users out even before the mode is applied.
    const mode_t previousMask = umask(S_IRWXG | S_IRWXO | S_IXUSR);
    const int    bindResult   = ::bind(fd, address.asSockaddr(), address.length);
    umask(previousMask);
    if (bindResult != 0) {
        LotusLogger::instance().error("Failed to bind " + std::string(label) + ": " + std::string(strerror(errno)));
        return false;
    }

    if (!address.path.empty() && chmod(address.path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        LotusLogger::instance().warn("Failed to restrict permissions on " + std::string(label) + " " + address.path + ": " + std::string(strerror(errno)));
    }

    address.bound = true;
    return true;
}

bool listenServerSocket(int fd, std::string_view label) {
    if (::listen(fd, kListenBacklog) != 0) {
        LotusLogger::instance().error("Failed to listen on " + std::string(label) + ": " + std::string(strerror(errno)));
        return false;
    }
    return true;
}
