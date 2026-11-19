// SPDX-License-Identifier: GPL-3.0-or-later
//
// A click can make a client commit the client-side preedit itself (clients with
// CapabilityFlag::ClientUnfocusCommit) and then report the new surrounding text
// while the addon still holds the same preedit. The addon must forget its stale
// copy so the next click cannot commit the same text again, but it must not
// touch the text the client owns.
#include "lotus-engine.h"
#include "test-input-context.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::fprintf(stderr, "Step: %s\nExpected: %s\nActual: %s\nMeaning: %s\n", step.c_str(), expected.c_str(), actual.c_str(), meaning.c_str());
    }

    bool expect(bool condition, const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        if (!condition) {
            reportFailure(step, expected, actual, meaning);
        }
        return condition;
    }

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        return event.accepted();
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-preedit-click-commit");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    auto                    context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // The click can arrive before any surrounding-text update, so the first
    // update after focus-in may already contain the client-committed preedit.
    if (!expect(send(engine, entry, *context, FcitxKey_a) && send(engine, entry, *context, FcitxKey_s), "compose without baseline", "both keys accepted", "key was not accepted",
                "the test cannot build a preedit"))
        return 1;
    context->surroundingText().setText("á\n", 1, 1);
    context->updateSurroundingText();
    if (!expect(context->commits().empty(), "click commit without baseline", "no commits", "commits=" + std::to_string(context->commits().size()),
                "the client owns the commit; the addon must not commit it again"))
        return 1;
    if (!expect(context->deletes().empty(), "click commit without baseline", "no deletes", "deletes=" + std::to_string(context->deletes().size()),
                "the text the client inserted must be left alone"))
        return 1;
    if (!expect(context->inputPanel().clientPreedit().toString().empty(), "click commit without baseline", "empty client preedit",
                "client preedit=" + context->inputPanel().clientPreedit().toString(), "the stale preedit must be dropped"))
        return 1;
    context->resetRecordedActions();

    // Give the state a baseline surrounding text, as a focus-in update would.
    context->surroundingText().setText("x", 1, 1);
    context->updateSurroundingText();

    // Compose "á" (Telex: a then s).
    if (!expect(send(engine, entry, *context, FcitxKey_a) && send(engine, entry, *context, FcitxKey_s), "compose", "both keys accepted", "key was not accepted",
                "the test cannot build a preedit"))
        return 1;
    if (!expect(context->inputPanel().clientPreedit().toString() == "á", "compose", "client preedit=á", "client preedit=" + context->inputPanel().clientPreedit().toString(),
                "the preedit must stay uncommitted while typing"))
        return 1;

    // The client commits the preedit on a click: "x" + "á", caret after it.
    context->surroundingText().setText("xá", 2, 2);
    context->updateSurroundingText();

    std::string actualDeletes;
    for (const auto& [offset, size] : context->deletes()) {
        actualDeletes += "(" + std::to_string(offset) + "," + std::to_string(size) + ")";
    }
    if (!expect(context->commits().empty(), "click commit", "no commits", "commits=" + std::to_string(context->commits().size()),
                "an accidental client commit must never be committed again"))
        return 1;
    if (!expect(context->deletes().empty(), "click commit", "no deletes", "deletes=" + actualDeletes, "the text the client inserted must be left alone"))
        return 1;
    if (!expect(context->inputPanel().clientPreedit().toString().empty(), "click commit", "empty client preedit",
                "client preedit=" + context->inputPanel().clientPreedit().toString(), "the stale preedit must be dropped"))
        return 1;

    // A double-click usually selects the committed run before the update arrives.
    // The stale preedit must be dropped here as well.
    context->resetRecordedActions();
    context->surroundingText().setText("y", 1, 1);
    context->updateSurroundingText();
    if (!expect(send(engine, entry, *context, FcitxKey_a) && send(engine, entry, *context, FcitxKey_s), "second compose", "both keys accepted", "key was not accepted",
                "the test cannot rebuild a preedit"))
        return 1;
    context->surroundingText().setText("yá", 2, 1);
    context->updateSurroundingText();
    if (!expect(context->deletes().empty(), "selected commit", "no deletes", "deletes=" + std::to_string(context->deletes().size()),
                "the text the client selected must be left alone"))
        return 1;
    if (!expect(context->inputPanel().clientPreedit().toString().empty(), "selected commit", "empty client preedit",
                "client preedit=" + context->inputPanel().clientPreedit().toString(), "the stale preedit must be dropped"))
        return 1;

    // A pure caret move must not cancel the composition.
    context->resetRecordedActions();
    context->surroundingText().setText("z", 1, 1);
    context->updateSurroundingText();
    if (!expect(send(engine, entry, *context, FcitxKey_a) && send(engine, entry, *context, FcitxKey_s), "third compose", "both keys accepted", "key was not accepted",
                "the test cannot rebuild a preedit"))
        return 1;
    context->surroundingText().setText("z", 0, 0);
    context->updateSurroundingText();
    if (!expect(context->deletes().empty(), "caret move", "no deletes", "deletes=" + std::to_string(context->deletes().size()), "a caret move must not delete client text"))
        return 1;
    if (!expect(context->inputPanel().clientPreedit().toString() == "á", "caret move", "client preedit=á", "client preedit=" + context->inputPanel().clientPreedit().toString(),
                "a caret move alone must not drop the composition"))
        return 1;

    std::printf("lotus preedit click commit: all checks passed\n");
    return 0;
}
