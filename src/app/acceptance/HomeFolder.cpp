// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Home on iPhone and iPad (the app folder, --app-folder): a project saved
// there is shown as being in "OpenShape (Files app)", where the Files app
// shows it, not at the app's sandbox path (/var/mobile/Containers/Data/
// Application/<id>/Documents), which meant nothing to anyone (and showed
// in the App Store screenshots). Saved and opened by clicking, as a user.

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtQuick/QQuickItem>

#include <memory>

namespace os::app {
namespace {

struct HomeFolderState {
    std::unique_ptr<QTemporaryDir> dir;
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
            r.check(text == QStringLiteral("OpenShape (Files app)"), "home folder: in OpenShape's folder of the Files app", text);
            r.check(!text.contains(QDir::toNativeSeparators(QDir::tempPath())) && !text.contains(QLatin1Char('/'))
                        && !text.contains(QLatin1Char('\\')),
                    "home folder: not the folder's path on the device", text);
            r.screenshot(QStringLiteral("homefolder_card"));
            r.check(r.clickItem(QStringLiteral("homeBack")), "home folder: back to the project");
        },
        [&r] {
            r.check(!r.app().homeVisible(), "home folder: Home closed");
            r.app().setAppFolder({});
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("homefolder"), 107, steps});

} // namespace
} // namespace os::app
