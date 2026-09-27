// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// About -> Licenses (docs/LICENSING.md): reached by clicking, it lists
// OpenShape, every library built into the app and the third-party code
// inside Qt; each entry shows its full license text (the LGPL 3.0 with the
// GPL 3.0 it builds on for Qt, the LGPL 2.1 with the Open CASCADE exception
// and its notice for OCCT, ...); "Your rights" is the LGPL relinking notice
// and written source offer, filled in with this build's version and source.
// Back and Esc return to the list; at iPhone size the card fits the screen
// inside the safe area.

#include "app/AcceptanceRunner.h"
#include "core/Version.h"
#include "ui/AppController.h"

#include <QtCore/QVariantMap>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <memory>

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

QRectF sceneRect(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF();
}

// The page shown: the title and that its text holds every phrase.
void checkPage(AcceptanceRunner& r, const QString& title, const QStringList& phrases, const QString& what)
{
    r.check(shown(r, QStringLiteral("licensePage")) && !shown(r, QStringLiteral("licensesList")),
            "licenses: " + what + " opens as a page");
    r.check(textOf(r, QStringLiteral("licensesTitle")) == title, "licenses: the page is titled " + title,
            textOf(r, QStringLiteral("licensesTitle")));
    const QString text = textOf(r, QStringLiteral("licenseText"));
    QStringList missing;
    for (const QString& phrase : phrases)
        if (!text.contains(phrase))
            missing << phrase;
    r.check(missing.isEmpty(), "licenses: " + what + " shows " + phrases.join(QStringLiteral(" / ")),
            "missing: " + missing.join(QStringLiteral(" / ")) + " in " + QString::number(text.size()) + " characters");
}

Steps steps(AcceptanceRunner& r)
{
    const QString version = QString::fromLatin1(kAppVersion);
    Steps steps;
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "licenses: File menu"); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("aboutMenuItem")), "licenses: About OpenShape"); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("aboutOverlay")), "licenses: the About card is open");
        r.check(r.clickItem(QStringLiteral("aboutLicenses")), "licenses: the About card's Licenses button");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("licensesOverlay")) && shown(r, QStringLiteral("licensesList")),
                "licenses: Licenses opens on its list");
        r.check(textOf(r, QStringLiteral("licensesTitle")) == QStringLiteral("Licenses"), "licenses: titled Licenses");
        // Everything built into the app, and the third-party code inside Qt.
        QStringList missing;
        for (const char* id : {"source-offer", "openshape", "occt", "qt", "planegcs", "freetype", "libzip", "json", "eigen",
                               "notosans", "qt:harfbuzz-ng", "qt:pcre2", "qt:libpng", "qt:unicode-cldr"})
            if (!r.findItem(QStringLiteral("license_") + QString::fromLatin1(id)))
                missing << QString::fromLatin1(id);
        r.check(missing.isEmpty(), "licenses: the list has OpenShape, every library and Qt's third-party parts",
                "missing: " + missing.join(QStringLiteral(", ")));
        int insideQt = 0;
        for (const QVariant& entry : r.app().licenseEntries())
            insideQt += entry.toMap().value(QStringLiteral("group")).toString() == QStringLiteral("qt") ? 1 : 0;
        r.check(insideQt >= 30, "licenses: at least 30 third-party parts of Qt are listed", QString::number(insideQt));
        r.screenshot(QStringLiteral("licenses_01_list"));
        r.check(r.clickItem(QStringLiteral("license_qt")), "licenses: tap Qt");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        checkPage(r, QStringLiteral("Qt"),
                  {QStringLiteral("LGPL-3.0-only"), QStringLiteral("The Qt Company"),
                   QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE"), QStringLiteral("Version 3, 29 June 2007"),
                   QStringLiteral("GNU GENERAL PUBLIC LICENSE"), QStringLiteral("download.qt.io")},
                  QStringLiteral("Qt"));
        r.screenshot(QStringLiteral("licenses_02_qt"));
        r.check(r.clickItem(QStringLiteral("licensesBack")), "licenses: Back");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("licensesList")) && !shown(r, QStringLiteral("licensePage")),
                "licenses: Back returns to the list");
        r.check(r.clickItem(QStringLiteral("license_occt")), "licenses: tap Open CASCADE Technology");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        checkPage(r, QStringLiteral("Open CASCADE Technology"),
                  {QStringLiteral("makes use of facilities provided by the Open CASCADE Technology software"),
                   QStringLiteral("Open CASCADE exception (version 1.0)"), QStringLiteral("Version 2.1, February 1999")},
                  QStringLiteral("Open CASCADE"));
        r.key(Qt::Key_Escape);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("licensesOverlay")) && shown(r, QStringLiteral("licensesList")),
                "licenses: Esc on a page returns to the list");
        r.check(r.clickItem(QStringLiteral("license_source-offer")), "licenses: tap Your rights to the LGPL libraries");
    });
    wait(steps, 2);
    steps.push_back([&r, version] {
        checkPage(r, QStringLiteral("Your rights to the LGPL libraries"),
                  {QStringLiteral("This is OpenShape ") + version, QStringLiteral("You may modify these libraries"),
                   QStringLiteral("https://github.com/SamuelAirs/openshape"),
                   QStringLiteral("Rebuilding the iOS app with modified libraries"), QStringLiteral("Written offer"),
                   QStringLiteral("at least three years")},
                  QStringLiteral("the LGPL notice"));
        const QString text = textOf(r, QStringLiteral("licenseText"));
        r.check(!text.contains(QLatin1Char('@')), "licenses: the notice has every placeholder filled in", text.left(300));
        r.check(r.clickItem(QStringLiteral("licensesBack")), "licenses: Back");
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("license_openshape")), "licenses: tap OpenShape"); });
    wait(steps, 2);
    steps.push_back([&r, version] {
        checkPage(r, QStringLiteral("OpenShape"),
                  {QStringLiteral("OpenShape ") + version, QStringLiteral("Mozilla Public License Version 2.0")},
                  QStringLiteral("OpenShape"));
        r.check(r.clickItem(QStringLiteral("licensesBack")), "licenses: Back");
    });
    wait(steps, 2);
    // Far down the list (clickItem scrolls it into view).
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("license_qt:unicode-cldr")), "licenses: tap Unicode CLDR (inside Qt)"); });
    wait(steps, 2);
    steps.push_back([&r] {
        checkPage(r, QStringLiteral("Unicode Common Locale Data Repository (CLDR)"),
                  {QStringLiteral("Unicode-3.0"), QStringLiteral("Unicode, Inc."), QStringLiteral("part of Qt")},
                  QStringLiteral("a third-party part of Qt"));
        r.key(Qt::Key_Escape);
    });
    wait(steps, 1);
    steps.push_back([&r] { r.key(Qt::Key_Escape); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("licensesOverlay")) && shown(r, QStringLiteral("aboutOverlay")),
                "licenses: Esc on the list closes Licenses, back to the About card");
        // ---- At iPhone size, touch layout, with the Dynamic Island's margins.
        r.app().setTouchMode(true);
        r.resizeWindow(402, 874);
        r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
    });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("aboutLicenses")), "licenses (phone): Licenses in About"); });
    wait(steps, 3);
    steps.push_back([&r] {
        const QRectF card = sceneRect(r, QStringLiteral("licensesCard"));
        const QRectF close = sceneRect(r, QStringLiteral("licensesClose"));
        const QRectF row = sceneRect(r, QStringLiteral("license_qt"));
        const auto rect = [](const QRectF& q) {
            return QStringLiteral("%1,%2 %3x%4").arg(q.x()).arg(q.y()).arg(q.width()).arg(q.height());
        };
        r.check(card.top() >= 62 && card.bottom() <= 874 - 34 && card.left() >= 0 && card.right() <= 402,
                "licenses (phone): the card fits inside the safe area", rect(card));
        r.check(card.contains(close) && close.height() >= 44, "licenses (phone): Close is on the card and touch-sized",
                rect(close));
        r.check(row.height() >= 44 && row.width() > 300, "licenses (phone): the rows are touch-sized", rect(row));
        r.screenshot(QStringLiteral("licenses_03_phone"));
        r.check(r.clickItem(QStringLiteral("license_freetype")), "licenses (phone): tap FreeType");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        checkPage(r, QStringLiteral("FreeType"),
                  {QStringLiteral("Portions of this software are copyright"), QStringLiteral("The FreeType Project LICENSE")},
                  QStringLiteral("FreeType (phone)"));
        const QRectF page = sceneRect(r, QStringLiteral("licensePage"));
        r.check(page.width() > 300 && page.height() > 400, "licenses (phone): the text fills the card",
                QStringLiteral("%1x%2").arg(page.width()).arg(page.height()));
        r.screenshot(QStringLiteral("licenses_04_phone_text"));
        r.check(r.clickItem(QStringLiteral("licensesClose")), "licenses (phone): Close");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("licensesOverlay")), "licenses (phone): Close closes Licenses");
        r.key(Qt::Key_Escape); // the About card
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(!shown(r, QStringLiteral("aboutOverlay")), "licenses: the About card closes too"); });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("licenses"), 96, steps});

} // namespace
} // namespace os::app
