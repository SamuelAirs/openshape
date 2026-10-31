// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Kernel robustness code adapted from OpenShape3D (MIT,
// third_party/openshape3d): its copyright line and the full MIT text are
// reachable by clicking About -> Licenses -> OpenShape3D, as the MIT license
// asks and the App Store release checklist (docs/LICENSING.md) checks.

#include "app/AcceptanceRunner.h"

#include <QtQuick/QQuickItem>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int frames)
{
    for (int i = 0; i < frames; ++i)
        steps.push_back([] {});
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

QString textOf(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item ? item->property("text").toString() : QString();
}

Steps steps(AcceptanceRunner& r)
{
    Steps steps;
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "openshape3d: File menu"); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("aboutMenuItem")), "openshape3d: About OpenShape"); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("aboutLicenses")), "openshape3d: the About card's Licenses button"); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("licensesList")), "openshape3d: Licenses opens on its list");
        r.check(r.findItem(QStringLiteral("license_openshape3d")) != nullptr, "openshape3d: the list has OpenShape3D");
        r.check(r.clickItem(QStringLiteral("license_openshape3d")), "openshape3d: tap OpenShape3D (Laan Labs)");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("licensePage")), "openshape3d: its page opens");
        r.check(textOf(r, QStringLiteral("licensesTitle")) == QStringLiteral("OpenShape3D (Laan Labs)"),
                "openshape3d: the page is titled OpenShape3D (Laan Labs)", textOf(r, QStringLiteral("licensesTitle")));
        const QString text = textOf(r, QStringLiteral("licenseText"));
        QStringList missing;
        for (const QString& phrase :
             {QStringLiteral("MIT"), QStringLiteral("Copyright (c) 2026 Laan Labs and the openshape3d contributors"),
              QStringLiteral("Permission is hereby granted, free of charge"), QStringLiteral("THE SOFTWARE IS PROVIDED \"AS IS\""),
              QStringLiteral("parts of the geometry code and the view cube (adapted)"), QStringLiteral("https://github.com/laanlabs/openshape3d")})
            if (!text.contains(phrase))
                missing << phrase;
        r.check(missing.isEmpty(), "openshape3d: the page shows the MIT license with Laan Labs' copyright",
                "missing: " + missing.join(QStringLiteral(" / ")));
        r.screenshot(QStringLiteral("openshape3d_license"));
        r.check(r.clickItem(QStringLiteral("licensesBack")), "openshape3d: Back");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("licensesList")), "openshape3d: Back returns to the list");
        r.check(r.clickItem(QStringLiteral("licensesClose")), "openshape3d: Close");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("licensesOverlay")), "openshape3d: Licenses closes");
        r.key(Qt::Key_Escape); // the About card
    });
    wait(steps, 1);
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("openshape3d"), 108, steps});

} // namespace
} // namespace os::app
