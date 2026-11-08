/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-unix-socket.h
 * @brief Filesystem AF_UNIX address helper for fcitx5-lotus-server sockets.
 */

#ifndef _LOTUS_UNIX_SOCKET_H_
#define _LOTUS_UNIX_SOCKET_H_

#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

/**
 * @brief A filesystem AF_UNIX address together with the length bind() needs.
 *
 * The socket file is removed when the owner is destroyed, but only after bind() succeeded, so a
 * failed bind never deletes a socket owned by another server.
 */
struct UnixSocket {
    struct sockaddr_un address{};     ///< Address filled from path
    socklen_t          length = 0;    ///< Length to pass to bind()/connect()
    std::string        path;          ///< Filesystem path of the socket
    bool               bound = false; ///< Whether this object owns the socket file

    UnixSocket()                             = default;
    UnixSocket(const UnixSocket&)            = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;

    UnixSocket(UnixSocket&& other) noexcept : address(other.address), length(other.length), path(std::move(other.path)), bound(other.bound) {
        other.path.clear();
        other.bound = false;
    }

    UnixSocket& operator=(UnixSocket&&) = delete;

    ~UnixSocket() {
        if (bound) {
            ::unlink(path.c_str());
        }
    }

    /**
     * @brief Views the address as the generic sockaddr bind() expects.
     * @return Borrowed pointer to the embedded address, owned by this object.
     */
    [[nodiscard]] const struct sockaddr* asSockaddr() const {
        return reinterpret_cast<const struct sockaddr*>(&address); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }
};

/**
 * @brief Builds a filesystem socket address, truncating to the kernel limit.
 * @param path Filesystem path of the socket.
 * @return Address ready for bind().
 */
UnixSocket makeUnixSocket(std::string path);

#endif // _LOTUS_UNIX_SOCKET_H_
