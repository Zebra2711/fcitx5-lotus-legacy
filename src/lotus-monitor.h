/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-monitor.h
 * @brief Input monitoring and timing utilities for fcitx5-lotus.
 */

#ifndef _FCITX5_LOTUS_MONITOR_H_
#define _FCITX5_LOTUS_MONITOR_H_

#include <string>

/**
 * @brief Checks that the mouse socket server runs as the current user.
 *
 * Uses SO_PEERCRED for the UID and the peer executable for the program check.
 * A privilege-dropped server is not dumpable, so /proc/<pid>/exe is hidden
 * from same-user processes; in that case argv[0] from /proc/<pid>/cmdline is
 * used instead.
 *
 * @param sock Connected mouse socket.
 * @param out_exe_path Receives the resolved peer executable path.
 * @return True when the peer is the expected server executable of this user.
 */
bool authenticateMouseSocketPeer(int sock, std::string& out_exe_path);
#endif // _FCITX5_LOTUS_MONITOR_H_
