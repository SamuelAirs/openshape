// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Home on iPhone and iPad (the app folder, --app-folder): a project saved
// there is shown as being in "OpenShape (Files app)", where the Files app
// shows it, not at the app's sandbox path (/var/mobile/Containers/Data/
// Application/<id>/Documents), which meant nothing to anyone (and showed
// in the App Store screenshots). Saved and opened by clicking, as a user.
// File > Open there is Home, not the system picker (which closed the app on
// the iPhone Duo, TD-158): a project in a folder made in the Files app is a
// card of its own and opens from it. On the Duo (simulated) Home's Open...
// and Import STEP... explain sharing from the Files app instead.

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <memory>

namespace os::app {
namespace {

struct HomeFolderState {
    std::unique_ptr<QTemporaryDir> dir;
    QString bracketCard; // the objectName of the card of the project in Parts/
};

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<HomeFolderState>();
    return {
        [&r, s] {
            s->dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-homefolder-XXXXXX"));
            r.check(s->dir->isValid(), "home folder: a temporary app folder");
            r.app().setAppFolder(s->dir->path());
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "home folder: File menu"); },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("saveMenuItem")), "home folder: File → Save"); },
        [] {},
        [&r] { r.type(QStringLiteral("Key tag")); },
        [&r, s] {
            r.check(r.clickItem(QStringLiteral("saveNameSave")), "home folder: Save in the name prompt");
            r.check(QFileInfo::exists(s->dir->filePath(QStringLiteral("Key tag.openshape"))),
                    "home folder: saved into the app folder");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "home folder: File menu again"); },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("homeMenuItem")), "home folder: File → Home"); },
        [] {},
        [] {},
        [&r] {
            // The newest project is the first card.
            QQuickItem* folder = r.findItem(QStringLiteral("homeCardFolder_0"));
            const QString text = folder ? folder->property("text").toString() : QString();
            r.check(folder && folder->isVisible(), "home folder: the card says where the project is");
            r.check(text == QStringLiteral("FoldForm (Files app)"), "home folder: in FoldForm's folder of the Files app", text);
            r.check(!text.contains(QDir::toNativeSeparators(QDir::tempPath())) && !text.contains(QLatin1Char('/'))
                        && !text.contains(QLatin1Char('\\')),
                    "home folder: not the folder's path on the device", text);
            r.screenshot(QStringLiteral("homefolder_card"));
            // An iPhone's insets: Home's title and buttons keep clear of the
            // status bar and Dynamic Island, as the other full-screen cards do.
            r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
        },
        [] {},
        [&r] {
            QQuickItem* page = r.findItem(QStringLiteral("homePage"));
            QQuickItem* newButton = r.findItem(QStringLiteral("homeNew"));
            const qreal pageTop = page ? page->mapToScene({0, 0}).y() : -1;
            const qreal buttonTop = newButton ? newButton->mapToScene({0, 0}).y() : -1;
            r.check(pageTop >= 62, "home folder: Home starts below the top inset", QString::number(pageTop));
            r.check(buttonTop >= 62 + 16, "home folder: New project below the top inset", QString::number(buttonTop));
            r.window()->setProperty("simulatedSafeArea", QVariant());
        },
        [] {},
        [&r] {
            QQuickItem* page = r.findItem(QStringLiteral("homePage"));
            r.check(page && page->mapToScene({0, 0}).y() == 0, "home folder: no inset on the desktop");
            r.check(r.clickItem(QStringLiteral("homeBack")), "home folder: back to the project");
        },
        [&r, s] {
            r.check(!r.app().homeVisible(), "home folder: Home closed");
            // A folder made in the Files app, with a project moved into it.
            const QString parts = s->dir->filePath(QStringLiteral("Parts"));
            r.check(QDir().mkpath(parts) && QFile::copy(s->dir->filePath(QStringLiteral("Key tag.openshape")),
                                                         parts + QStringLiteral("/Bracket.openshape")),
                    "home folder: a project in a folder of its own");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "home folder: File menu for Open"); },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("openMenuItem")), "home folder: File → Open…"); },
        [] {},
        [] {},
        [&r, s] {
            r.check(r.app().homeVisible(), "home folder: File → Open shows Home, not the system picker");
            int bracket = -1;
            for (int i = 0; i < 8 && bracket < 0; ++i) {
                const QQuickItem* name = r.findItem(QStringLiteral("homeCardName_%1").arg(i));
                if (name && name->property("text").toString() == QStringLiteral("Bracket"))
                    bracket = i;
            }
            r.check(bracket >= 0, "home folder: the project in the folder is a card");
            const QQuickItem* folder = r.findItem(QStringLiteral("homeCardFolder_%1").arg(bracket));
            const QString text = folder ? folder->property("text").toString() : QString();
            r.check(text == QStringLiteral("FoldForm → Parts (Files app)"), "home folder: its card names the folder", text);
            s->bracketCard = QStringLiteral("homeCard_%1").arg(bracket);
        },
        [&r, s] { r.check(r.clickItem(s->bracketCard), "home folder: tap the folder's project"); },
        [] {},
        [&r] {
            r.check(!r.app().homeVisible(), "home folder: Home closed for the project");
            r.check(r.app().documentTitle() == QStringLiteral("Bracket"), "home folder: the folder's project is open",
                    r.app().documentTitle());
            // The iPhone Duo: Home's Open... and Import STEP... explain the
            // Files app's Share instead of the picker that shows no files.
            r.check(r.app().filePickerWorks(), "home folder: the picker is used off the Duo");
            r.app().simulateIphoneDuo(true);
            r.app().setHomeVisible(true);
        },
        [&r] { r.check(r.clickItem(QStringLiteral("homeOpen")), "home folder: Open… on the Duo"); },
        [] {},
        [&r] {
            QQuickItem* card = r.findItem(QStringLiteral("shareFromFiles"));
            QQuickItem* text = r.findItem(QStringLiteral("shareFromFilesText"));
            r.check(card && card->isVisible(), "home folder: the Duo explains sharing from the Files app");
            r.check(text && text->property("text").toString().contains(QStringLiteral("touch and hold the project")),
                    "home folder: how to share a project", text ? text->property("text").toString() : QString());
        },
        [&r] { r.check(r.clickItem(QStringLiteral("shareFromFilesCancel")), "home folder: Cancel"); },
        [] {},
        [&r] {
            QQuickItem* card = r.findItem(QStringLiteral("shareFromFiles"));
            r.check(card && !card->isVisible(), "home folder: Cancel closes it");
            r.check(r.app().homeVisible(), "home folder: Home is still there");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("homeImport")), "home folder: Import STEP… on the Duo"); },
        [] {},
        [&r] {
            QQuickItem* text = r.findItem(QStringLiteral("shareFromFilesText"));
            r.check(text && text->isVisible() && text->property("text").toString().contains(QStringLiteral("STEP file")),
                    "home folder: how to share a STEP file", text ? text->property("text").toString() : QString());
        },
        [&r] { r.check(r.clickItem(QStringLiteral("shareFromFilesOpen")), "home folder: Open Files"); },
        [] {},
        [&r] {
            QQuickItem* card = r.findItem(QStringLiteral("shareFromFiles"));
            r.check(card && !card->isVisible(), "home folder: Open Files closes it (and switches to Files on iOS)");
            r.app().simulateIphoneDuo(false);
            r.app().setHomeVisible(false);
            r.app().setAppFolder({});
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("homefolder"), 107, steps});

} // namespace
} // namespace os::app
