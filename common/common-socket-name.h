/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoàng Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/*
 * Header-only because the addon and the server are two separate targets that both include it from
 * common/, and the server must not link the addon core.
 */

#ifndef _COMMON_SOCKET_NAME_H_
#define _COMMON_SOCKET_NAME_H_

#include <array>
#include <cstddef>
#include <cstdlib>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/un.h>
#include <unistd.h>

/**
 * @brief Environment variable that replaces the user segment of the socket paths.
 */
inline constexpr const char* LOTUS_SOCKET_NAMESPACE_ENV = "LOTUS_SOCKET_NAMESPACE";

/**
 * @brief Resolves the user segment of the socket path.
 * @param username Default segment, the name of the current user.
 * @return Value of LOTUS_SOCKET_NAMESPACE_ENV when set and non-empty, @p username otherwise.
 */
inline std::string lotusSocketNamespace(std::string_view username) {
    const char* override_value = std::getenv(LOTUS_SOCKET_NAMESPACE_ENV);
    if (override_value != nullptr && *override_value != '\0') {
        return override_value;
    }
    return std::string(username);
}

/**
 * @brief Runtime directory that holds the per-user sockets.
 *
 * For the logged-in user this is @c XDG_RUNTIME_DIR, the same directory ydotool uses. A system
 * service that starts before the user logs in has no @c XDG_RUNTIME_DIR, so it falls back to the
 * conventional @c /run/user/<uid> path.
 *
 * @param username User owning the sockets.
 * @return Absolute runtime directory path.
 */
inline std::string lotusRuntimeDir(std::string_view username) {
    if (const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR"); runtime_dir != nullptr && *runtime_dir != '\0') {
        return runtime_dir;
    }

    uid_t                  uid = getuid();
    struct passwd          pw{};
    struct passwd*         result = nullptr;
    std::array<char, 1024> buffer{};
    if (getpwnam_r(std::string(username).c_str(), &pw, buffer.data(), buffer.size(), &result) == 0 && result != nullptr) {
        uid = result->pw_uid;
    }
    return "/run/user/" + std::to_string(uid);
}

/**
 * @brief Builds the filesystem path of a per-user Unix socket.
 *
 * The result is truncated to the kernel's sun_path limit; both the addon and the server use this
 * helper, so they always agree on the final path.
 *
 * @param name Socket name, e.g. "kb" or "mouse".
 * @param username User owning the socket.
 * @return Absolute socket path.
 */
inline std::string lotusSocketPath(std::string_view name, std::string_view username) {
    std::string path = lotusRuntimeDir(username);
    path += "/.lotus_";
    path += lotusSocketNamespace(username);
    path += '_';
    path += name;
    path += ".socket";

    constexpr std::size_t kMaxSocketPathLength = sizeof(sockaddr_un::sun_path) - 1U;
    if (path.size() > kMaxSocketPathLength) {
        path.resize(kMaxSocketPathLength);
    }
    return path;
}

#endif // _COMMON_SOCKET_NAME_H_
