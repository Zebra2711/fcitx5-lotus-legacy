/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file common-peer-credentials.h
 * @brief SO_PEERCRED based access checks shared by the addon and the uinput server.
 */

#ifndef _COMMON_PEER_CREDENTIALS_H_
#define _COMMON_PEER_CREDENTIALS_H_

#include <array>
#include <climits> // IWYU pragma: keep
#include <optional>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Credentials the kernel recorded for the peer of an AF_UNIX socket.
 */
struct LotusPeerCredentials {
    pid_t pid = -1;                     ///< Peer process ID
    uid_t uid = static_cast<uid_t>(-1); ///< Peer user ID
    gid_t gid = static_cast<gid_t>(-1); ///< Peer group ID
};

/**
 * @brief Reads the kernel-recorded peer credentials of a connected AF_UNIX socket.
 * @param fd Connected socket descriptor.
 * @return Peer credentials, or std::nullopt when SO_PEERCRED is unavailable.
 */
inline std::optional<LotusPeerCredentials> lotusPeerCredentials(int fd) {
    struct ucred native{};
    socklen_t    length = sizeof(native);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &native, &length) != 0 || length != sizeof(native)) {
        return std::nullopt;
    }
    return LotusPeerCredentials{.pid = native.pid, .uid = native.uid, .gid = native.gid};
}

/**
 * @brief Resolves the executable behind a peer PID.
 * @param pid Peer process ID.
 * @return Absolute executable path, or an empty string when /proc/<pid>/exe cannot be read.
 */
inline std::string lotusPeerExecutablePath(pid_t pid) {
    const std::string          link = "/proc/" + std::to_string(pid) + "/exe";
    std::array<char, PATH_MAX> path{};
    const ssize_t              length = ::readlink(link.c_str(), path.data(), path.size() - 1U);
    if (length <= 0) {
        return {};
    }
    return std::string(path.data(), static_cast<std::size_t>(length));
}

/**
 * @brief Reads the executable path from a peer's argv[0].
 *
 * Services that drop privileges lose their dumpable flag, which makes
 * /proc/<pid>/exe unreadable even for a process running as the same user.
 * /proc/<pid>/cmdline stays readable in that case, so client-side checks fall
 * back to it instead of rejecting an otherwise valid server.
 *
 * @param pid Peer process ID.
 * @return First argv entry, or an empty string when /proc/<pid>/cmdline cannot be read.
 */
inline std::string lotusPeerCommandPath(pid_t pid) {
    const std::string          path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::array<char, PATH_MAX> buffer{};
    const int                  fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    const ssize_t length = ::read(fd, buffer.data(), buffer.size() - 1U);
    ::close(fd);
    if (length <= 0) {
        return {};
    }
    buffer[static_cast<std::size_t>(length)] = '\0';
    return std::string(buffer.data());
}

#endif // _COMMON_PEER_CREDENTIALS_H_
