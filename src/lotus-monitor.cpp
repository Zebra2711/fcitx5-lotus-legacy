/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "common-executable-path.h"
#include "common-peer-credentials.h"
#include "lotus-log.h"
#include "lotus-monitor.h"

#include <string>

#include <sys/socket.h>
#include <unistd.h>

bool authenticateMouseSocketPeer(int sock, std::string& out_exe_path) {
    const auto credentials = lotusPeerCredentials(sock);
    if (!credentials.has_value()) {
        LOTUS_ERROR("Failed to get peer credentials");
        return false;
    }
    if (credentials->uid != getuid()) {
        LOTUS_ERROR("Refusing mouse socket peer from UID " + std::to_string(credentials->uid));
        return false;
    }

    out_exe_path = lotusPeerExecutablePath(credentials->pid);
    if (out_exe_path.empty()) {
        // Privilege-dropped services are not dumpable, so a same-user process
        // cannot read /proc/<pid>/exe. argv[0] stays readable and is enough to
        // recognize the expected server here.
        out_exe_path = lotusPeerCommandPath(credentials->pid);
    }
    if (out_exe_path.empty()) {
        LOTUS_ERROR("Failed to resolve the mouse socket peer executable");
        return false;
    }

    return isAllowedExecutablePath(out_exe_path, LOTUS_SERVER_EXECUTABLE, LOTUS_ALT_EXECUTABLE_PREFIX);
}
