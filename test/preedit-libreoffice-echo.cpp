// SPDX-License-Identifier: GPL-3.0-or-later
//
// LibreOffice reports surrounding text that contains the active preedit on every
// key. That echo is not a client-side commit, so the preedit must keep growing
// instead of being dropped after each key (which made Preedit mode unusable).
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

    std::string clientPreedit(TestInputContext& context) {
        return context.inputPanel().clientPreedit().toString();
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-preedit-libreoffice-echo");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);

    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");
    // The app name drives the LibreOffice quirk; the dbus frontend matches the
    // path LibreOffice uses through the GTK IM module.
    auto context = std::make_unique<TestInputContext>(&testInstance.instance, "soffice");
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->setFrontendName("dbus");
    context->surroundingText().setText("", 0, 0);
    context->focusIn();
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    fcitx::KeyEvent first(&*context, fcitx::Key(FcitxKey_e), false);
    engine.keyEvent(entry, first);
    if (!expect(first.accepted() && clientPreedit(*context) == "e", "first key", "accepted with preedit=e",
                "accepted=" + std::to_string(first.accepted()) + ", preedit=" + clientPreedit(*context), "the first key must start a composition"))
        return 1;

    // LibreOffice echoes the active preedit into the surrounding text while the
    // key is still being composed.
    context->surroundingText().setText("e", 1, 1);
    context->updateSurroundingText();
    if (!expect(clientPreedit(*context) == "e", "preedit echo", "preedit=e", "preedit=" + clientPreedit(*context),
                "the echoed preedit must not be mistaken for a client-side commit"))
        return 1;

    fcitx::KeyEvent second(&*context, fcitx::Key(FcitxKey_e), false);
    engine.keyEvent(entry, second);
    if (!expect(second.accepted() && clientPreedit(*context) == "ê", "second key", "accepted with preedit=ê",
                "accepted=" + std::to_string(second.accepted()) + ", preedit=" + clientPreedit(*context), "the composition must continue across the echo"))
        return 1;

    context->surroundingText().setText("ê", 1, 1);
    context->updateSurroundingText();
    if (!expect(clientPreedit(*context) == "ê", "second echo", "preedit=ê", "preedit=" + clientPreedit(*context), "the second echoed preedit must not reset the composition"))
        return 1;

    std::printf("lotus preedit libreoffice echo: all checks passed\n");
    return 0;
}
