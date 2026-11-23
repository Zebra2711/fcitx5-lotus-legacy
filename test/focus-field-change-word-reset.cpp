// SPDX-License-Identifier: GPL-3.0-or-later
//
// Clicking from one field into another makes Firefox lose and regain focus almost
// back to back. That fast pair used to look like the per-key focus bounce uinput
// modes must survive, so the word composed in the first field leaked into the
// second one and the next key replaced it (e in field 1, e in field 2 -> ê).
// A bounce only happens while a key is being handled; a click into another field
// arrives after typing has settled.
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <fcitx-utils/event.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            reportFailure("process key " + std::to_string(symbol), "accepted=" + std::to_string(static_cast<int>(requireAccepted)),
                          "accepted=" + std::to_string(static_cast<int>(event.accepted())) + ", commits=" + std::to_string(context.commits().size()),
                          "the field change must reset the word buffers");
            return false;
        }
        return true;
    }

    // Runs the fcitx event loop long enough for the 0 ms deferred-commit timer to fire.
    void pumpEventLoop(TestInstance& testInstance) {
        auto&      loop      = testInstance.instance.eventLoop();
        const auto exitTimer = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 5000, 0, [&loop](fcitx::EventSourceTime*, uint64_t) {
            loop.exit();
            return false;
        });
        loop.exec();
    }

    bool checkCommits(const TestInputContext& context, const std::vector<std::string>& expected, const char* step) {
        if (context.commits() == expected) {
            return true;
        }
        std::string actual = "commits=";
        for (const auto& commit : context.commits())
            actual += "['" + commit + "']";
        reportFailure(step, "commits=['·']['á']", actual, "the composed word from the previous field must not leak");
        return false;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-focus-field-change-word-reset");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth) {
        reportFailure("configure Smooth", "mode=Smooth", "configured mode differs", "the test needs uinput mode");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    context = std::make_unique<TestInputContext>(&testInstance.instance);
    // Firefox drives Lotus through the dbus frontend and reports no surrounding
    // text, which is also the path the click between fields takes.
    context->setFrontendName("dbus");
    context->focusIn();
    fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, in);

    // Compose "tu" in the first field.
    if (!send(engine, entry, *context, FcitxKey_t, false) || !send(engine, entry, *context, FcitxKey_u, false))
        return 1;

    // The user clicks the next field after typing has settled, so the focus
    // out/in pair that follows is a real field change, not a key bounce.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    fcitx::InputContextEvent out(context.get(), fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, out);
    engine.reset(entry, out);
    context->focusOut();

    context->focusIn();
    fcitx::InputContextEvent back(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, back);

    // The next keys must start a new word: "a" followed by "s" may only replace
    // that fresh "a" with "á", not the "tu" composed in the previous field.
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;

    KbMsg msg{};
    if (!listener.receive(msg, "the replacement for the fresh word did not arrive"))
        return 1;
    // Smooth mode deletes the fresh 'a' plus the dot anchor; a larger count
    // would mean the previous field's word leaked into this one.
    if (msg.op != KbOp::Backspace || msg.count != 2) {
        reportFailure("replacement after a field change", "op=backspace, count=2", "op=" + std::to_string(static_cast<int>(msg.op)) + ", count=" + std::to_string(msg.count),
                      "only the fresh 'a' and the dot anchor may be replaced; a larger count means the previous field's word leaked");
        return 1;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(msg.count); ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, false))
            return 1;
    }
    if (!send(engine, entry, *context, FcitxKey_Pause, true))
        return 1;

    pumpEventLoop(testInstance);
    if (!checkCommits(*context, {"·", "á"}, "commit in the new field"))
        return 1;

    std::printf("lotus focus field change word reset: all checks passed\n");
    return 0;
}
