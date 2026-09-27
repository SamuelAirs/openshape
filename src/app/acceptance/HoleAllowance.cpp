// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The FDM hole allowance ("hole_allowance"): chosen in Preferences by
// clicking a value and by typing one, remembered, and applied at once to the
// Hole tool's screw presets (clearance fits get it, the tap drill does not);
// the hole drilled with it has the widened diameter (exact volume).

#include "app/AcceptanceRunner.h"
#include "document/Body.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"
#include "ui/AppSettings.h"

#include <QtCore/QSettings>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>

namespace os::app {
namespace {

const interact::HoleOperation* holeTool(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::HoleOperation*>(r.app().interaction().operation());
}

QString diameterText(const interact::HoleOperation* tool)
{
    return tool ? AcceptanceRunner::num(tool->diameter()) : QStringLiteral("no Hole tool");
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto overlayVisible = [&r] {
        auto* overlay = r.findItem(QStringLiteral("preferencesOverlay"));
        return overlay && overlay->isVisible();
    };
    return {
        [&r] {
            r.check(std::abs(r.app().holeAllowance() - 0.2) < 1e-12, "allowance: 0.2 mm by default",
                    AcceptanceRunner::num(r.app().holeAllowance()));
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "allowance: a box");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(3, -3, 20));
            r.type(QStringLiteral("5"));
            r.key(Qt::Key_Return);
            r.check(std::abs(r.bodyHeight() - 5) < 1e-6, "allowance: a 20 x 20 x 5 plate", AcceptanceRunner::num(r.bodyHeight()));
            r.app().interaction().fitAll(false);
            r.check(r.clickItem(QStringLiteral("fileMenuButton")), "allowance: File menu");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("preferencesMenuItem")), "allowance: Preferences… in the File menu"); },
        [] {}, [] {},
        [&r, overlayVisible] {
            r.check(overlayVisible(), "allowance: the Preferences panel opens");
            r.check(r.clickItem(QStringLiteral("prefHoleAllowance_0.4")), "allowance: the 0.4 mm button");
            r.check(std::abs(r.app().holeAllowance() - 0.4) < 1e-12, "allowance: 0.4 mm chosen",
                    AcceptanceRunner::num(r.app().holeAllowance()));
            r.check(std::abs(r.app().interaction().holeAllowance() - 0.4) < 1e-12, "allowance: the modeling side has it");
            // Any value in 0-1 mm can be typed.
            r.check(r.clickItem(QStringLiteral("prefHoleAllowanceField")), "allowance: the typed-value field");
        },
        [&r] {
            r.type(QStringLiteral("0.25"));
            r.key(Qt::Key_Return);
        },
        [&r] {
            r.check(std::abs(r.app().holeAllowance() - 0.25) < 1e-12, "allowance: 0.25 mm typed",
                    AcceptanceRunner::num(r.app().holeAllowance()));
            QSettings settings;
            r.check(std::abs(ui::loadPreferences(settings).holeAllowance - 0.25) < 1e-12, "allowance: and remembered");
            auto* note = r.findItem(QStringLiteral("prefHoleAllowanceNote"));
            const QString text = note ? note->property("text").toString() : QString();
            r.check(text.contains(QStringLiteral("M3 close fit is then 3.45 mm")), "allowance: the note gives an example", text);
            r.screenshot(QStringLiteral("hole_allowance_preferences"));
            // Out of range: refused with a message, the value stays.
            r.check(r.clickItem(QStringLiteral("prefHoleAllowanceField")), "allowance: the field again");
        },
        [&r] {
            r.key(Qt::Key_A, Qt::ControlModifier);
            r.type(QStringLiteral("3"));
            r.key(Qt::Key_Return);
        },
        [&r] {
            auto* error = r.findItem(QStringLiteral("prefHoleAllowanceError"));
            const QString text = error ? error->property("text").toString() : QString();
            r.check(text == QStringLiteral("The allowance must be between 0 and 1 mm."), "allowance: 3 mm is refused", text);
            r.check(std::abs(r.app().holeAllowance() - 0.25) < 1e-12, "allowance: still 0.25 mm");
            r.check(r.clickItem(QStringLiteral("preferencesClose")), "allowance: Close");
        },
        [&r, overlayVisible] {
            r.check(!overlayVisible(), "allowance: the panel closes");
            // The plate's top face is still selected (it was pushed).
            r.check(r.clickItem(QStringLiteral("tool_hole")), "allowance: Hole in the Modify palette");
            r.check(holeTool(r) != nullptr, "allowance: the Hole tool", r.app().operationTitle());
        },
        // The tool remembers its last settings (an earlier scenario may have
        // left another size, a blind depth or a head): M3, through all, no head.
        [&r] { r.check(r.clickItem(QStringLiteral("action_size:2")), "allowance: M3"); },
        [&r] {
            if (const auto* tool = holeTool(r); tool && !tool->settings().throughAll)
                r.check(r.clickItem(QStringLiteral("action_throughAll")), "allowance: Through all");
        },
        [&r] {
            if (const auto* tool = holeTool(r); tool && tool->settings().head == doc::HoleKind::Counterbore)
                r.check(r.clickItem(QStringLiteral("action_head:counterbore")), "allowance: no counterbore");
            else if (tool && tool->settings().head == doc::HoleKind::Countersink)
                r.check(r.clickItem(QStringLiteral("action_head:countersink")), "allowance: no countersink");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("action_fit:normal")), "allowance: Normal fit"); },
        [&r] {
            const auto* tool = holeTool(r);
            r.check(tool && tool->settings().throughAll && tool->settings().head == doc::HoleKind::Plain,
                    "allowance: plain holes through all");
            r.check(tool && std::abs(tool->diameter() - 3.65) < 1e-9, "allowance: M3 normal fit is 3.4 + 0.25 mm", diameterText(tool));
            r.check(r.app().operationValueText() == QStringLiteral("3.65 mm"), "allowance: the chip shows 3.65 mm",
                    r.app().operationValueText());
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("action_fit:tap")), "allowance: Tap");
            const auto* tool = holeTool(r);
            r.check(tool && std::abs(tool->diameter() - 2.5) < 1e-9, "allowance: the M3 tap drill stays 2.5 mm", diameterText(tool));
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("action_fit:close")), "allowance: Close fit");
            const auto* tool = holeTool(r);
            r.check(tool && std::abs(tool->diameter() - 3.45) < 1e-9, "allowance: M3 close fit is 3.2 + 0.25 mm", diameterText(tool));
            r.click(r.screenPoint(0, 0, 5));
            r.check(tool && tool->positions().size() == 1, "allowance: a hole in the middle");
        },
        [&r] {
            r.key(Qt::Key_Return);
            const double expected = 2000 - kPi * 1.725 * 1.725 * 5;
            r.check(std::abs(r.bodyVolume() - expected) < 1e-3, "allowance: a 3.45 mm hole through the plate",
                    AcceptanceRunner::num(r.bodyVolume()));
            bool listed = false;
            for (const auto& row : r.app().interaction().historyRows())
                listed = listed || (row.name == "Hole" && row.detail.find("M3 close fit +0.25 mm") != std::string::npos);
            r.check(listed, "allowance: the Model panel names the allowance");
            r.screenshot(QStringLiteral("hole_allowance_hole"));
            // Back to 0: the standard ISO 273 sizes.
            r.key(Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));
        },
        [] {},
        [&r, overlayVisible] {
            r.check(overlayVisible(), "allowance: Ctrl+, opens Preferences");
            r.check(r.clickItem(QStringLiteral("prefHoleAllowance_0")), "allowance: the 0 button");
            r.check(r.app().holeAllowance() == 0.0, "allowance: none", AcceptanceRunner::num(r.app().holeAllowance()));
            r.key(Qt::Key_Escape);
            r.click(r.screenPoint(6, -6, 5));
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("action_hole")), "allowance: the face's Hole action");
            const auto* tool = holeTool(r);
            r.check(tool && std::abs(tool->diameter() - 3.2) < 1e-9, "allowance: M3 close fit is ISO 273's 3.2 mm again",
                    diameterText(tool));
            r.key(Qt::Key_Escape);
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("hole_allowance"), 74, steps});

} // namespace
} // namespace os::app
