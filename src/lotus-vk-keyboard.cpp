/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-vk-keyboard.h"
#include "lotus-utils.h"

#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

namespace fcitx {
    namespace {
        constexpr uint32_t kTextKeys[] = {
            2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
            30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
            43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 57,
        };
        constexpr size_t kTextKeysLen = sizeof(kTextKeys) / sizeof(kTextKeys[0]);

        struct RegistryData {
            wl_seat*                         seat    = nullptr;
            zwp_virtual_keyboard_manager_v1* manager = nullptr;
        };

        void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t /*version*/) {
            auto* d = static_cast<RegistryData*>(data);
            if (std::strcmp(interface, wl_seat_interface.name) == 0) {
                if (d->seat == nullptr) {
                    d->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
                }
            } else if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
                d->manager = static_cast<zwp_virtual_keyboard_manager_v1*>(
                    wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
            }
        }

        void registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

        const wl_registry_listener kRegistryListener = {registryGlobal, registryGlobalRemove};

        int createMemfd(const char* name) {
#ifdef SYS_memfd_create
            int fd = static_cast<int>(syscall(SYS_memfd_create, name, 1 /* MFD_CLOEXEC */));
            if (fd >= 0) {
                return fd;
            }
#endif
            char path[] = "/tmp/lotus-vk-keymap-XXXXXX";
            int tmpFd = mkstemp(path);
            if (tmpFd < 0) {
                return -1;
            }
            unlink(path);
            return tmpFd;
        }

        std::string xkbSymName(uint32_t cp) {
            char buf[16];
            if (cp <= 0xFFFF) {
                std::snprintf(buf, sizeof(buf), "U%04X", cp);
            } else {
                std::snprintf(buf, sizeof(buf), "U%08X", cp);
            }
            return buf;
        }
    } // namespace

    LotusVkKeyboard& LotusVkKeyboard::instance() {
        static LotusVkKeyboard k;
        return k;
    }

    LotusVkKeyboard::~LotusVkKeyboard() { disconnect(); }

    void LotusVkKeyboard::disconnect() {
        if (keyboard_ != nullptr) {
            zwp_virtual_keyboard_v1_destroy(keyboard_);
            keyboard_ = nullptr;
        }
        if (manager_ != nullptr) {
            zwp_virtual_keyboard_manager_v1_destroy(manager_);
            manager_ = nullptr;
        }
        if (seat_ != nullptr) {
            wl_seat_destroy(seat_);
            seat_ = nullptr;
        }
        if (registry_ != nullptr) {
            wl_registry_destroy(registry_);
            registry_ = nullptr;
        }
        if (display_ != nullptr) {
            wl_display_disconnect(display_);
            display_ = nullptr;
        }
    }

    bool LotusVkKeyboard::ensureConnected() {
        if (keyboard_ != nullptr) {
            return true;
        }
        display_ = wl_display_connect(nullptr);
        if (display_ == nullptr) {
            LOTUS_ERROR("FkUinput: cannot connect to Wayland display");
            return false;
        }
        registry_ = wl_display_get_registry(display_);
        RegistryData data;
        wl_registry_add_listener(registry_, &kRegistryListener, &data);
        wl_display_roundtrip(display_);
        seat_    = data.seat;
        manager_ = data.manager;
        if (seat_ == nullptr || manager_ == nullptr) {
            LOTUS_ERROR("FkUinput: compositor has no zwp_virtual_keyboard_manager_v1");
            disconnect();
            return false;
        }
        keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager_, seat_);
        if (keyboard_ == nullptr) {
            LOTUS_ERROR("FkUinput: failed to create virtual keyboard");
            disconnect();
            return false;
        }
        LOTUS_INFO("FkUinput: virtual keyboard ready");
        return true;
    }

    bool LotusVkKeyboard::available() { return ensureConnected(); }

    bool LotusVkKeyboard::uploadKeymap(const uint32_t* cps, size_t n) {
        if (n > kTextKeysLen) {
            return false;
        }
        const uint32_t maxXkb = kTextKeys[kTextKeysLen - 1] + 8U;
        std::string    keycodes;
        std::string    symbols;
        keycodes.reserve(64 + n * 24);
        symbols.reserve(64 + n * 40);
        keycodes += "    <BKSP> = 22;\n";
        symbols += "    key <BKSP> { [ BackSpace ] };\n";
        for (size_t i = 0; i < n; ++i) {
            const uint32_t xkb = kTextKeys[i] + 8U;
            char           name[16];
            std::snprintf(name, sizeof(name), "K%02zu", i);
            keycodes += "    <";
            keycodes += name;
            keycodes += "> = ";
            keycodes += std::to_string(xkb);
            keycodes += ";\n";
            symbols += "    key <";
            symbols += name;
            symbols += "> { [ ";
            symbols += xkbSymName(cps[i]);
            symbols += " ] };\n";
        }

        std::string keymap;
        keymap.reserve(256 + keycodes.size() + symbols.size());
        keymap += "xkb_keymap {\n";
        keymap += "xkb_keycodes \"lotus_vk\" {\n";
        keymap += "    minimum = 8;\n";
        keymap += "    maximum = ";
        keymap += std::to_string(maxXkb);
        keymap += ";\n";
        keymap += keycodes;
        keymap += "};\n";
        keymap += "xkb_types \"complete\" { include \"complete\" };\n";
        keymap += "xkb_compatibility \"complete\" { include \"complete\" };\n";
        keymap += "xkb_symbols \"lotus_vk\" {\n";
        keymap += "    name[Group1] = \"lotus_vk\";\n";
        keymap += symbols;
        keymap += "};\n";
        keymap += "};\n";

        const int fd = createMemfd("lotus-vk-keymap");
        if (fd < 0) {
            LOTUS_ERROR("FkUinput: memfd_create failed");
            return false;
        }
        const auto size = static_cast<uint32_t>(keymap.size() + 1);
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            close(fd);
            return false;
        }
        ssize_t written = 0;
        while (written < static_cast<ssize_t>(size)) {
            const ssize_t w = write(fd, keymap.c_str() + written, size - static_cast<uint32_t>(written));
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(fd);
                return false;
            }
            written += w;
        }
        lseek(fd, 0, SEEK_SET);
        zwp_virtual_keyboard_v1_keymap(keyboard_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
        close(fd);
        zwp_virtual_keyboard_v1_modifiers(keyboard_, 0, 0, 0, 0);
        return wl_display_roundtrip(display_) >= 0;
    }

    void LotusVkKeyboard::tap(uint32_t evdevKey, bool track) {
        if (track) {
            echo_.emplace_back(evdevKey + 8U, time_);
        }
        zwp_virtual_keyboard_v1_key(keyboard_, time_++, evdevKey, WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(keyboard_, time_++, evdevKey, WL_KEYBOARD_KEY_STATE_RELEASED);
    }

    bool LotusVkKeyboard::consumeEcho(uint32_t code, uint32_t time) {
        for (auto it = echo_.begin(); it != echo_.end(); ++it) {
            if (it->first == code && it->second == time) {
                echo_.erase(it);
                return true;
            }
        }
        return false;
    }

    bool LotusVkKeyboard::emit(int backspaces, std::string_view utf8, bool trackEcho) {
        if (backspaces < 0) {
            backspaces = 0;
        }
        std::vector<uint32_t> cps;
        if (!utf8.empty()) {
            if (!utf8::validate(utf8)) {
                LOTUS_ERROR("FkUinput: invalid UTF-8");
                return false;
            }
            for (auto cp : utf8::MakeUTF8CharRange(utf8)) {
                cps.push_back(static_cast<uint32_t>(cp));
            }
        }
        if (backspaces == 0 && cps.empty()) {
            return true;
        }
        if (!ensureConnected()) {
            return false;
        }
        injecting_ = true;
        if (echo_.size() > 512) {
            echo_.clear();
        }
        bool   ok    = true;
        bool   first = true;
        size_t pos   = 0;
        while (ok && (first || pos < cps.size())) {
            const size_t n = std::min(kTextKeysLen, cps.size() - pos);
            ok             = uploadKeymap(cps.data() + pos, n);
            if (!ok) {
                break;
            }
            if (first) {
                for (int i = 0; i < backspaces; ++i) {
                    tap(KEY_BACKSPACE, trackEcho);
                }
                first = false;
            }
            for (size_t i = 0; i < n; ++i) {
                tap(kTextKeys[i], trackEcho);
            }
            wl_display_roundtrip(display_);
            pos += n;
        }
        injecting_ = false;
        if (!ok) {
            echo_.clear();
        }
        if (!ok) {
            LOTUS_ERROR("FkUinput: keymap upload failed");
        }
        return ok;
    }
} // namespace fcitx
