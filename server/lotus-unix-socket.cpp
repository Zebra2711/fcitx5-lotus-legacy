/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-unix-socket.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

UnixSocket makeUnixSocket(std::string path) {
    UnixSocket socket;
    socket.address.sun_family = AF_UNIX;
    const std::size_t length  = std::min(path.size(), sizeof(socket.address.sun_path) - 1U);
    std::memcpy(socket.address.sun_path, path.data(), length);
    socket.address.sun_path[length] = '\0';
    socket.path                     = std::move(path);
    socket.path.resize(length);
    socket.length = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + length + 1U);
    return socket;
}
