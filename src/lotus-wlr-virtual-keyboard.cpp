/*
 * SPDX-FileCopyrightText: 2026 Zebra2711
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-wlr-virtual-keyboard.h"
#include "common-fd-guard.h"
#include "lotus-log.h"
#include "lotus-utils.h"

#include <atomic>
#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <array>
#include <utility>
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
    void WlDisplayDeleter::operator()(wl_display* display) const noexcept {
        if (display != nullptr) {
            wl_display_disconnect(display);
        }
    }

    void WlRegistryDeleter::operator()(wl_registry* registry) const noexcept {
        if (registry != nullptr) {
            wl_registry_destroy(registry);
        }
    }

    void WlSeatDeleter::operator()(wl_seat* seat) const noexcept {
        if (seat != nullptr) {
            wl_seat_destroy(seat);
        }
    }

    void WlKeyboardDeleter::operator()(wl_keyboard* keyboard) const noexcept {
        if (keyboard != nullptr) {
            wl_keyboard_destroy(keyboard);
        }
    }

    void ZwpVirtualKeyboardManagerDeleter::operator()(zwp_virtual_keyboard_manager_v1* manager) const noexcept {
        if (manager != nullptr) {
            zwp_virtual_keyboard_manager_v1_destroy(manager);
        }
    }

    void ZwpVirtualKeyboardDeleter::operator()(zwp_virtual_keyboard_v1* keyboard) const noexcept {
        if (keyboard != nullptr) {
            zwp_virtual_keyboard_v1_destroy(keyboard);
        }
    }

    namespace {
        // Linux evdev codes for the printable section of a PC keyboard. Chromium may
        // fall back to a key's physical meaning even when its keysym is printable.
        // Never put literal text on Escape, Backspace, Tab, Enter, or modifier codes.
        constexpr auto     kTextKeys = std::to_array<uint32_t>({
            2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
            30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 57,
        });
        constexpr uint32_t kShiftMod = 1;

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
            } else if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0 && d->manager == nullptr) {
                d->manager = static_cast<zwp_virtual_keyboard_manager_v1*>(wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
            }
        }

        void                       registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

        const wl_registry_listener kRegistryListener = {.global = registryGlobal, .global_remove = registryGlobalRemove};

        int                        createMemfd(const char* name) {
#ifdef SYS_memfd_create
            int fd = static_cast<int>(syscall(SYS_memfd_create, name, 1 /* MFD_CLOEXEC */));
            if (fd >= 0) {
                return fd;
            }
#endif
            auto path  = std::to_array("/tmp/lotus-vk-keymap-XXXXXX");
            int  tmpFd = mkostemp(path.data(), O_CLOEXEC);
            if (tmpFd < 0) {
                return -1;
            }
            unlink(path.data());
            return tmpFd;
        }

        std::string xkbSymName(uint32_t cp) {
            std::array<char, 16> buf{};
            if (cp <= 0xFFFF) {
                std::snprintf(buf.data(), buf.size(), "U%04X", cp);
            } else {
                std::snprintf(buf.data(), buf.size(), "U%08X", cp);
            }
            return buf.data();
        }

        // These events belong to a wl_keyboard we only use to read the seat keymap.
        void ignoreKeyboardEnter(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, wl_surface* /*surface*/, wl_array* /*keys*/) {}

        void ignoreKeyboardLeave(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, wl_surface* /*surface*/) {}

        void ignoreKeyboardKey(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, uint32_t /*time*/, uint32_t /*key*/, uint32_t /*state*/) {}

        void ignoreKeyboardModifiers(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, uint32_t /*depressed*/, uint32_t /*latched*/, uint32_t /*locked*/,
                                     uint32_t /*group*/) {}

        void ignoreKeyboardRepeatInfo(void* /*data*/, wl_keyboard* /*keyboard*/, int32_t /*rate*/, int32_t /*delay*/) {}

        constexpr std::string_view kCustomKeymapName = "lotus-wlr-virtual-keyboard";
    } // namespace

    LotusWlrVirtualKeyboard& LotusWlrVirtualKeyboard::instance() {
        static LotusWlrVirtualKeyboard k;
        return k;
    }

    LotusWlrVirtualKeyboard::~LotusWlrVirtualKeyboard() {
        cancelPendingTasks();
        disconnect();
    }

    void LotusWlrVirtualKeyboard::disconnect() {
        // Never leave a mirrored Shift behind when the device goes away.
        setShift(false);
        // Proxies first, then the display that owns the underlying connection.
        keyboard_.reset();
        keymapObserver_.reset();
        keymapSet_          = false;
        keymapIsCompositor_ = false;
        compositorKeymap_.clear();
        manager_.reset();
        seat_.reset();
        registry_.reset();
        display_.reset();
    }

    bool LotusWlrVirtualKeyboard::probe() {
        if (display_) {
            return manager_ != nullptr && seat_ != nullptr;
        }
        display_.reset(wl_display_connect(nullptr));
        if (!display_) {
            return false;
        }
        registry_.reset(wl_display_get_registry(display_.get()));
        RegistryData data; // Non-owning: the proxies move into the members below.
        wl_registry_add_listener(registry_.get(), &kRegistryListener, &data);
        if (wl_display_roundtrip(display_.get()) < 0) {
            disconnect();
            return false;
        }
        seat_.reset(data.seat);
        manager_.reset(data.manager);
        if (!seat_ || !manager_) {
            disconnect();
            return false;
        }
        return true;
    }

    bool LotusWlrVirtualKeyboard::ensureDevice() {
        if (keyboard_) {
            return true;
        }
        if (!probe()) {
            return false;
        }
        watchCompositorKeymap();
        keyboard_.reset(zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager_.get(), seat_.get()));
        if (!keyboard_) {
            disconnect();
            return false;
        }
        return true;
    }

    bool LotusWlrVirtualKeyboard::available() {
        std::scoped_lock lock(deviceMutex_);
        if (probed_) {
            return supported_;
        }
        probed_ = true;
        if (getEnv("WAYLAND_DISPLAY").empty()) {
            supported_ = false;
            LOTUS_INFO("no WAYLAND_DISPLAY, wlr virtual keyboard unavailable, using uinput");
            return supported_;
        }
        supported_ = probe();
        if (supported_) {
            LOTUS_INFO("compositor supports zwp_virtual_keyboard_v1, using the wlr virtual keyboard");
        } else {
            LOTUS_INFO("compositor has no zwp_virtual_keyboard_v1, using uinput");
        }
        return supported_;
    }

    bool LotusWlrVirtualKeyboard::uploadKeymapBytes(std::string_view keymap) {
        FdGuard fd(createMemfd("lotus-wlr-virtual-keyboard-keymap"));
        if (!fd.isValid()) {
            LOTUS_ERROR("wlr-virtual-keyboard: memfd_create failed");
            return false;
        }
        const auto size = static_cast<uint32_t>(keymap.size() + 1);
        if (ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
            return false;
        }
        size_t written = 0;
        while (written < keymap.size()) {
            const ssize_t w = write(fd.get(), keymap.data() + written, keymap.size() - written); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            written += static_cast<size_t>(w);
        }
        lseek(fd.get(), 0, SEEK_SET);
        zwp_virtual_keyboard_v1_keymap(keyboard_.get(), WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd.get(), size);
        keymapSet_ = true;
        // A keymap upload resets the client's xkb state, so mirror the user's Shift in the modifiers.
        zwp_virtual_keyboard_v1_modifiers(keyboard_.get(), userShift_.load(std::memory_order_acquire) ? kShiftMod : 0, 0, 0, 0);
        return wl_display_roundtrip(display_.get()) >= 0;
    }

    bool LotusWlrVirtualKeyboard::uploadKeymap(const uint32_t* cps, size_t n) {
        if (n > kTextKeys.size()) {
            return false;
        }
        const uint32_t maxXkb = std::max(kTextKeys.back() + 8U, KEY_DELETE + 8U);
        std::string    keycodes;
        std::string    symbols;
        keycodes.reserve(128 + (n * 24));
        symbols.reserve(160 + (n * 40));
        keycodes += "    <BKSP> = ";
        keycodes += std::to_string(KEY_BACKSPACE + 8U);
        keycodes += ";\n    <LFSH> = ";
        keycodes += std::to_string(KEY_LEFTSHIFT + 8U);
        keycodes += ";\n    <LEFT> = ";
        keycodes += std::to_string(KEY_LEFT + 8U);
        keycodes += ";\n    <DELE> = ";
        keycodes += std::to_string(KEY_DELETE + 8U);
        keycodes += ";\n";
        symbols += "    key <BKSP> { [ BackSpace ] };\n";
        symbols += "    key <LFSH> { [ Shift_L ] };\n";
        symbols += "    key <LEFT> { [ Left ] };\n";
        symbols += "    key <DELE> { [ Delete ] };\n";
        symbols += "    modifier_map Shift { <LFSH> };\n";
        for (size_t i = 0; i < n; ++i) {
            const uint32_t    xkb  = kTextKeys[i] + 8U; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
            const std::string name = "K" + (i < 10 ? std::string("0") : std::string()) + std::to_string(i);
            keycodes += "    <";
            keycodes += name;
            keycodes += "> = ";
            keycodes += std::to_string(xkb);
            keycodes += ";\n";
            symbols += "    key <";
            symbols += name;
            symbols += "> { type = \"TWO_LEVEL\", [ ";
            symbols += xkbSymName(cps[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            symbols += ", ";
            symbols += xkbSymName(cps[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            symbols += " ] };\n";
        }

        std::string keymap;
        keymap.reserve(256 + keycodes.size() + symbols.size());
        keymap += "xkb_keymap {\n";
        keymap += "xkb_keycodes \"lotus-wlr-virtual-keyboard\" {\n";
        keymap += "    minimum = 8;\n";
        keymap += "    maximum = ";
        keymap += std::to_string(maxXkb);
        keymap += ";\n";
        keymap += keycodes;
        keymap += "};\n";
        keymap += "xkb_types \"complete\" { include \"complete\" };\n";
        keymap += "xkb_compatibility \"complete\" { include \"complete\" };\n";
        keymap += "xkb_symbols \"lotus_vk\" {\n";
        keymap += "    name[Group1] = \"lotus-wlr-virtual-keyboard\";\n";
        keymap += symbols;
        keymap += "};\n";
        keymap += "};\n";

        if (!uploadKeymapBytes(keymap)) {
            return false;
        }
        keymapIsCompositor_ = false;
        return true;
    }

    bool LotusWlrVirtualKeyboard::restoreCompositorKeymap() {
        if (compositorKeymap_.empty()) {
            return false;
        }
        if (keymapIsCompositor_ && keymapSet_) {
            return true;
        }
        if (!uploadKeymapBytes(compositorKeymap_)) {
            return false;
        }
        keymapIsCompositor_ = true;
        return true;
    }

    void LotusWlrVirtualKeyboard::watchCompositorKeymap() {
        if (keymapObserver_ || !seat_) {
            return;
        }
        keymapObserver_.reset(wl_seat_get_keyboard(seat_.get()));
        if (!keymapObserver_) {
            return;
        }
        static const wl_keyboard_listener listener = {
            .keymap      = &LotusWlrVirtualKeyboard::compositorKeymap,
            .enter       = ignoreKeyboardEnter,
            .leave       = ignoreKeyboardLeave,
            .key         = ignoreKeyboardKey,
            .modifiers   = ignoreKeyboardModifiers,
            .repeat_info = ignoreKeyboardRepeatInfo,
        };
        wl_keyboard_add_listener(keymapObserver_.get(), &listener, this);
        // The compositor sends the active keyboard's keymap as soon as the resource is created.
        wl_display_roundtrip(display_.get());
    }

    void LotusWlrVirtualKeyboard::storeCompositorKeymap(uint32_t format, int fd, uint32_t size) {
        // The fd belongs to this event; the client closes it.
        FdGuard guard(fd);
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
            return;
        }
        std::string data(size, '\0');
        uint32_t    total = 0;
        while (total < size) {
            const ssize_t r = pread(guard.get(), data.data() + total, size - total, total); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return;
            }
            if (r == 0) {
                break;
            }
            total += static_cast<uint32_t>(r);
        }
        size_t len = total;
        while (len > 0 && data[len - 1] == '\0') {
            --len;
        }
        if (len == 0) {
            return;
        }
        // Our own keymaps are echoed back whenever the virtual keyboard owns the
        // seat; only remember keymaps that belong to the real keyboards.
        if (std::string_view(data.data(), len).find(kCustomKeymapName) != std::string_view::npos) {
            return;
        }
        compositorKeymap_.assign(data.data(), len);
    }

    void LotusWlrVirtualKeyboard::compositorKeymap(void* data, wl_keyboard* /*keyboard*/, uint32_t format, int32_t fd, uint32_t size) {
        static_cast<LotusWlrVirtualKeyboard*>(data)->storeCompositorKeymap(format, fd, size);
    }

    void LotusWlrVirtualKeyboard::tap(uint32_t evdevKey, bool track) {
        if (track) {
            echo_.emplace_back(evdevKey + 8U, time_);
        }
        zwp_virtual_keyboard_v1_key(keyboard_.get(), time_++, evdevKey, WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(keyboard_.get(), time_++, evdevKey, WL_KEYBOARD_KEY_STATE_RELEASED);
    }

    void LotusWlrVirtualKeyboard::setShift(bool down) {
        if (!keyboard_ || !keymapSet_) {
            return;
        }
        zwp_virtual_keyboard_v1_modifiers(keyboard_.get(), down ? kShiftMod : 0, 0, 0, 0);
        wl_display_flush(display_.get());
    }

    void LotusWlrVirtualKeyboard::setUserShift(bool down) {
        userShift_.store(down, std::memory_order_release);
        std::scoped_lock lock(deviceMutex_);
        setShift(down);
    }

    bool LotusWlrVirtualKeyboard::consumeEcho(uint32_t code, uint32_t time) {
        std::scoped_lock lock(deviceMutex_);
        for (auto it = echo_.begin(); it != echo_.end(); ++it) {
            if (it->first == code && it->second == time) {
                echo_.erase(it);
                return true;
            }
        }
        return false;
    }

    bool LotusWlrVirtualKeyboard::emit(int backspaces, std::string_view utf8, bool trackEcho) {
        return emitImpl(backspaces, utf8, trackEcho, false);
    }

    bool LotusWlrVirtualKeyboard::emitSelect(int count, std::string_view utf8, bool trackEcho) {
        return emitImpl(count, utf8, trackEcho, true);
    }

    bool LotusWlrVirtualKeyboard::emitImpl(int count, std::string_view utf8, bool trackEcho, bool shiftLeft) {
        count = std::max(count, 0);
        LOTUS_INFO("wlr emit count=" + std::to_string(count) + " utf8=" + std::string(utf8));
        std::vector<uint32_t> cps;
        if (!utf8.empty()) {
            if (!utf8::validate(utf8)) {
                LOTUS_ERROR("wlr-virtual-keyboard: invalid UTF-8");
                return false;
            }
            for (auto cp : utf8::MakeUTF8CharRange(utf8)) {
                cps.push_back(static_cast<uint32_t>(cp));
            }
        }
        if (count == 0 && cps.empty()) {
            return true;
        }
        std::scoped_lock lock(deviceMutex_);
        if (!ensureDevice()) {
            return false;
        }
        injecting_.store(true, std::memory_order_release);
        if (echo_.size() > 512) {
            echo_.clear();
        }
        const auto moveKeys = [&] {
            if (shiftLeft) {
                setShift(true);
                wl_display_roundtrip(display_.get());
                for (int i = 0; i < count; ++i) {
                    tap(KEY_LEFT, trackEcho);
                }
                setShift(false);
                wl_display_roundtrip(display_.get());
                if (!cps.empty()) {
                    tap(KEY_DELETE, trackEcho);
                }
                // The selection taps end with Shift off; restore the user's Shift for later forwarded keys.
                if (userShift_.load(std::memory_order_acquire)) {
                    setShift(true);
                }
            } else {
                for (int i = 0; i < count; ++i) {
                    tap(KEY_BACKSPACE, trackEcho);
                }
            }
        };
        bool   ok  = true;
        size_t pos = 0;
        if (cps.empty()) {
            // Movement-only bursts stay on the compositor keymap so its xkb state is not recreated.
            if (!restoreCompositorKeymap()) {
                ok = uploadKeymap(nullptr, 0);
            }
            if (ok) {
                moveKeys();
                wl_display_roundtrip(display_.get());
            }
        } else {
            bool first = true;
            while (ok && (first || pos < cps.size())) {
                const size_t n = std::min(kTextKeys.size(), cps.size() - pos);
                ok             = uploadKeymap(cps.data() + pos, n); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                if (!ok) {
                    break;
                }
                if (first) {
                    moveKeys();
                    first = false;
                }
                for (size_t i = 0; i < n; ++i) {
                    tap(kTextKeys[i], trackEcho); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
                }
                wl_display_roundtrip(display_.get());
                pos += n;
            }
            if (ok) {
                restoreCompositorKeymap();
            }
        }
        injecting_.store(false, std::memory_order_release);
        setShift(userShift_.load(std::memory_order_acquire));
        if (!ok) {
            echo_.clear();
            LOTUS_ERROR("wlr-virtual-keyboard: keymap upload failed");
        }
        return ok;
    }
} // namespace fcitx
