/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-kb-sender.h"

#include "common-peer-credentials.h"
#include "lotus-log.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

KbSender::KbSender(std::string socketPath) : socketPath_(std::move(socketPath)) {}

KbSender::~KbSender() {
    close();
}

bool KbSender::connected() const {
    std::scoped_lock lock(mutex_);
    return fd_.isValid();
}

bool KbSender::ensureConnected() {
    std::scoped_lock lock(mutex_);
    if (fd_.isValid()) {
        return true;
    }
    return connectLocked();
}

bool KbSender::send(const KbMsg& msg) {
    std::scoped_lock lock(mutex_);
    if (!fd_.isValid()) {
        connectLocked();
    }
    if (!fd_.isValid()) {
        LOTUS_ERROR("Cannot send key request since cannot connect to uinput server");
        return false;
    }
    const ssize_t written = ::send(fd_.get(), &msg, sizeof(msg), MSG_NOSIGNAL);
    if (std::cmp_equal(written, sizeof(msg))) {
        return true;
    }
    LOTUS_WARN("Failed to send key request: " + std::string(std::strerror(errno)));
    fd_.reset();
    if (!connectLocked()) {
        return false;
    }
    const ssize_t retried = ::send(fd_.get(), &msg, sizeof(msg), MSG_NOSIGNAL);
    if (std::cmp_equal(retried, sizeof(msg))) {
        LOTUS_INFO("Reconnected to uinput server successfully");
        return true;
    }
    LOTUS_WARN("Failed to send key request after reconnect: " + std::string(std::strerror(errno)));
    return false;
}

void KbSender::close() {
    std::scoped_lock lock(mutex_);
    fd_.reset();
}

bool KbSender::connectLocked() {
    sockaddr_un address{};
    address.sun_family    = AF_UNIX;
    const auto pathLength = std::min(socketPath_.size(), sizeof(address.sun_path) - 1U);
    std::memcpy(address.sun_path, socketPath_.data(), pathLength);
    address.sun_path[pathLength] = '\0';
    const auto length            = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + pathLength + 1U);

    FdGuard    candidate(socket(AF_UNIX, SOCK_SEQPACKET, 0));
    if (!candidate.isValid()) {
        LOTUS_ERROR("Failed to create keyboard socket: " + std::string(std::strerror(errno)));
        return false;
    }
    if (connect(candidate.get(), reinterpret_cast<const sockaddr*>(&address), length) != 0) {
        LOTUS_ERROR("Failed to connect to socket: " + std::string(std::strerror(errno)));
        return false;
    }

    const auto peer = lotusPeerCredentials(candidate.get());
    if (!peer.has_value() || peer->uid != getuid()) {
        LOTUS_ERROR("Refusing a keyboard socket that is not owned by the current user");
        return false;
    }

    fd_ = std::move(candidate);
    return true;
}
