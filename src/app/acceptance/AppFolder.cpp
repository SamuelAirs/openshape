// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Saving and exporting as on an iPhone or iPad (iOS has no save dialog):
// with an app folder set (on iOS the Documents folder the Files app shows;
// here a temporary one, as with --app-folder), File → Save asks for a name
// only, Save As replaces or renames, exports go into Exports, and "Save
// changes?" → Save asks for the name before continuing.

#include "app/AcceptanceRunner.h"
#include "geometry/Modeling.h"
#include "io/ProjectFile.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

struct FolderState {
    std::unique_ptr<QTemporaryDir> dir;
    QString path(const QString& relative) const { return dir->path() + QLatin1Char('/') + relative; }
};

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

std::size_t bodiesIn(const QString& file)
{
    auto loaded = io::loadProject(std::filesystem::path(file.toStdWString()));
    return loaded ? loaded.value()->bodies().size() : 0;
}

// File → <item> (the menu opens first).
Steps fileMenu(AcceptanceRunner& r, const QString& item, const QString& what)
{
    return {
        [&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "app folder: File menu"); },
        [&r, item, what] { r.check(r.clickItem(item), what); },
    };
}

void append(Steps& steps, Steps more)
{
    for (auto& step : more)
        steps.push_back(std::move(step));
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<FolderState>();
    Steps steps;
    steps.push_back([&r, s] {
        s->dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-appfolder-XXXXXX"));
        r.check(s->dir->isValid(), "app folder: a temporary folder for the run");
        r.app().setAppFolder(s->dir->path());
        r.check(r.app().savesToAppFolder(), "app folder: saving goes to the app folder (as on iOS)");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(r.app().bodyCount() == 1, "app folder: a box to save");
    });
    wait(steps, 3);
    // ---- Save without a file: the name prompt, not a file dialog.
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("app folder: File → Save")));
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("saveNamePrompt")), "app folder: Save asks for a name");
        QQuickItem* save = r.findItem(QStringLiteral("saveNameSave"));
        r.check(save && !save->isEnabled(), "app folder: no Save without a name");
        r.type(QStringLiteral("Bracket"));
    });
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("saveNameSave")), "app folder: Save in the name prompt");
        r.check(!shown(r, QStringLiteral("saveNamePrompt")), "app folder: the prompt closes");
        const QString file = s->path(QStringLiteral("Bracket.openshape"));
        r.check(QFileInfo::exists(file), "app folder: saved as Bracket.openshape in the app folder", file);
        r.check(r.app().documentTitle() == QStringLiteral("Bracket") && !r.app().dirty(), "app folder: the document is Bracket, saved",
                r.app().documentTitle());
        r.check(bodiesIn(file) == 1, "app folder: the file holds the box");
        // A second box; Save now writes the same file (no prompt).
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 3);
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("app folder: File → Save again")));
    steps.push_back([&r, s] {
        r.check(!shown(r, QStringLiteral("saveNamePrompt")), "app folder: a saved project saves without asking");
        r.check(bodiesIn(s->path(QStringLiteral("Bracket.openshape"))) == 2 && !r.app().dirty(),
                "app folder: Bracket.openshape now holds two boxes");
    });
    // ---- Save As: the current name offered; the same name replaces.
    append(steps, fileMenu(r, QStringLiteral("saveAsMenuItem"), QStringLiteral("app folder: File → Save As")));
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("saveNamePrompt")), "app folder: Save As asks for a name");
        QQuickItem* field = r.findItem(QStringLiteral("saveNameField"));
        r.check(field && field->property("text").toString() == QStringLiteral("Bracket"), "app folder: it offers the current name",
                field ? field->property("text").toString() : QString());
        QQuickItem* save = r.findItem(QStringLiteral("saveNameSave"));
        r.check(save && save->property("text").toString() == QStringLiteral("Replace"), "app folder: the same name says Replace",
                save ? save->property("text").toString() : QString());
        r.screenshot(QStringLiteral("appfolder_save_as"));
        r.type(QStringLiteral("Lid")); // the offered name is selected: typing replaces it
    });
    steps.push_back([&r, s] {
        QQuickItem* save = r.findItem(QStringLiteral("saveNameSave"));
        r.check(save && save->property("text").toString() == QStringLiteral("Save"), "app folder: a new name says Save");
        r.key(Qt::Key_Return); // Enter in the name field saves
        r.check(QFileInfo::exists(s->path(QStringLiteral("Lid.openshape"))) && r.app().documentTitle() == QStringLiteral("Lid"),
                "app folder: Enter saved it as Lid.openshape", r.app().documentTitle());
        r.check(QFileInfo::exists(s->path(QStringLiteral("Bracket.openshape"))), "app folder: Bracket.openshape is still there");
    });
    // ---- Exports go into Exports, named after the project.
    append(steps, fileMenu(r, QStringLiteral("exportStlMenuItem"), QStringLiteral("app folder: File → Export STL")));
    append(steps, fileMenu(r, QStringLiteral("export3mfMenuItem"), QStringLiteral("app folder: File → Export 3MF")));
    append(steps, fileMenu(r, QStringLiteral("exportStepMenuItem"), QStringLiteral("app folder: File → Export STEP")));
    steps.push_back([&r, s] {
        for (const char* file : {"Exports/Lid.stl", "Exports/Lid.3mf", "Exports/Lid.step"}) {
            const QFileInfo info(s->path(QString::fromLatin1(file)));
            r.check(info.exists() && info.size() > 100, QStringLiteral("app folder: %1 written").arg(QString::fromLatin1(file)),
                    QString::number(info.size()) + QStringLiteral(" bytes"));
        }
        r.check(!shown(r, QStringLiteral("saveNamePrompt")), "app folder: exports ask nothing");
    });
    // ---- "Save changes?" → Save on a new document: the name first, then New goes on.
    steps.push_back([&r] {
        r.app().newDocument();
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 3);
    append(steps, fileMenu(r, QStringLiteral("newMenuItem"), QStringLiteral("app folder: File → New with unsaved changes")));
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("unsavedDialog")), "app folder: Save changes? is asked");
        r.check(r.clickItem(QStringLiteral("unsavedSave")), "app folder: Save changes? → Save");
    });
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("saveNamePrompt")), "app folder: then the name is asked");
        r.type(QStringLiteral("Third"));
    });
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("saveNameSave")), "app folder: Save the name");
        r.check(bodiesIn(s->path(QStringLiteral("Third.openshape"))) == 1, "app folder: Third.openshape holds the box");
        r.check(r.app().bodyCount() == 0 && r.app().documentTitle() == QStringLiteral("Untitled"),
                "app folder: then New went on (an empty Untitled)", r.app().documentTitle());
        // Cancel keeps things as they are.
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 3);
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("app folder: File → Save (to cancel)")));
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("saveNameCancel")), "app folder: Cancel in the name prompt");
        r.check(!shown(r, QStringLiteral("saveNamePrompt")) && r.app().dirty() && !r.app().hasProjectPath(),
                "app folder: Cancel saves nothing");
        r.check(QDir(s->dir->path()).entryList({QStringLiteral("*.openshape")}, QDir::Files).size() == 3,
                "app folder: three projects in the folder");
        r.screenshot(QStringLiteral("appfolder_done"));
        r.app().setAppFolder({});
        r.app().newDocument(); // (the runner's reset would ask nothing either)
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("appfolder"), 95, steps});

} // namespace
} // namespace os::app
