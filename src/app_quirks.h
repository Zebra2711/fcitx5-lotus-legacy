/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file app_quirks.h
 * @brief List of applications requiring acknowledgment workaround.
 *
 * These browsers need special handling for uinput mode to work correctly.
 */

#include <string>
#include <string_view>

/*
 * TODO: allow users to set delay config per app
 * set default config in /etc
 * appname - delay BS - delay commit
 *
 * Delay BS is the time delay between each BS send. Currently handled in
 * lotus-uinput-server.
 *
 * Delay commit counts the time when the server sends a signal that it has
 * finished sending BS to the fcitx5 client. The issue is we don't know when
 * the app receives all BS and processes them completely.
 *
 *  n <= 7 ?
 *  __________      n BS       __________
 *  | server |  <-----------  |  fcitx5 |
 *  |________|                |_________|
 *        |        n+1 BS          |
 *        |------------------->[filter(if count <= n)]---|
 *                                | true                 | false
 *             ______    sent BS  |                      |
 *            | APP |<-A----------       commit          |
 *            ------ <-B---------------------------------|
 *            (***)
 *
 *    ok, A first then B, this is good? NOOOOOO....
 *
 *    in (***):
 *    fcitx5          app            input label
 *    | ---....._A____>| -._              |
 *    |                |    ^~. n BS      |
 *    |---....._B_____>|-._    ^~.        |
 *    |                |   ^~-----^~.__B->|
 *    |                |              ^~. |
 *    |                |              A ^>|
 *
 *    BS needs to be processed n times, but commit should happen only once.
 *    That is why processing BS events is slow due to TOCTOU.
 *    During BS processing, if commit happens, bad things will occur.
 *
 *    Most common issue without delay:
 *    lao + s -> lao + 2BS? + áo -> lao + 1BS + á + nothing (1BS + o)
 *    result: laá  WRONG
 *    OR: luong + w -> luong + 4BS? + ương -> luong + 1BS + ư (ương + 3BS?)
 *    result: luonư  WRONG
 *
 *    Another issue is a Firefox bug when using ACK:
 *    During BS sending, it blocks other key events:
 *    luong + w -> luong + 1BS + null (ương is blocked) + 1BS + 1BS + 1BS
 *    result: l  WRONG
 *
 *    NOTE: n BS? means it is not a single event, but multiple key events
 *    (e.g., 3BS? = 1BS + 1BS + 1BS)
 */

/**
 * @brief List of application names requiring ACK workaround.
 *
 * Chromium-based browsers that need special handling for text replacement.
 */
inline constexpr std::array<std::string_view, 19> ack_apps = {
  "chrome", "chromium", "brave", "edge", "vivaldi",
  "opera", "coccoc", "cromite", "helium", "thorium",
  "slimjet", "yandex", "vesktop", "obsidian", "mullvad",
  "firefox", "zen", "waterfox", "wps"
};

/**
 * @brief List of application names have goood support surrowding text
 *
 */
inline constexpr std::array<std::string_view, 1> surrtp_apps = {
  "soffice"
};

inline constexpr std::array<std::string_view, 5> terminalm = {
  "foot", "kitty", "alacritty", "ghostty", "st"
};
