// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Home's project cards: Rename..., Duplicate and Delete... in a card's menu
// (the idea is OpenShape3D's project gallery). A project saved into a
// temporary app folder (as on an iPhone) is duplicated (two cards, the copy
// has every byte and its preview), the copy renamed (refused while the name
// is taken or unusable; then the card shows the new name and the old file
// is gone), deleted after the confirmation (into a stand-in trash that is
// another temporary folder: the Recycle Bin is never touched), and deleted
// for good where the trash fails, after a second confirmation. Renaming the
// open project renames the window; deleting it leaves an empty document. A
// project outside OpenShape's folder is never deleted for good. Then the
// same on a phone-sized window by touch (a long press opens the menu).
// Every file this scenario makes or changes lies in its temporary folders.

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <memory>

namespace os::app {
namespace {

struct HomeProjectsState {
    std::unique_ptr<QTemporaryDir> folder;  // the app folder
    std::unique_ptr<QTemporaryDir> trash;   // where the stand-in trash puts files
    std::unique_ptr<QTemporaryDir> outside; // a project outside the app folder
    qint64 originalSize = 0;
    QPointF pressAt;
    int card = -1; // the card whose menu is open
};

// The index of the card showing project `name` (-1: none).
int cardOf(AcceptanceRunner& r, const QString& name)
{
    const QVariantList projects = r.app().homeProjects();
    for (int i = 0; i < projects.size(); ++i)
        if (projects[i].toMap().value(QStringLiteral("name")).toString() == name)
            return i;
    return -1;
}

QString itemText(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item ? item->property("text").toString() : QString();
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

QString cardNames(AcceptanceRunner& r)
{
    QStringList names;
    for (const QVariant& project : r.app().homeProjects())
        names << project.toMap().value(QStringLiteral("name")).toString();
    return names.join(QStringLiteral(", "));
}

// One pass: `where` names it in the checks ("desktop", "phone"); with
// `touch` everything is tapped with a finger and the first menu is opened
// by a long press.
void addPass(std::vector<AcceptanceRunner::Step>& steps, AcceptanceRunner& r, const std::shared_ptr<HomeProjectsState>& s,
             const QString& where, bool touch)
{
    auto say = [where](const QString& text) { return QStringLiteral("home projects (") + where + QStringLiteral("): ") + text; };
    auto press = [&r, touch](const QString& name) { return touch ? r.tapItem(name) : r.clickItem(name); };
    const QString part = QStringLiteral("Part ") + where;
    const QString copy = part + QStringLiteral(" copy");
    const QString spare = QStringLiteral("Spare ") + where;
    const QString wall = QStringLiteral("Wall ") + where;
    auto file = [s](const QString& name) { return s->folder->filePath(name + QStringLiteral(".openshape")); };
    // Opens the menu of the card showing project `name` (cards are in recent
    // order, which Save and Duplicate change); entryOf() names its entries.
    auto menuOf = [&r, s, press](const QString& name) {
        s->card = cardOf(r, name);
        return press(QStringLiteral("homeCardMenu_%1").arg(s->card));
    };
    auto entryOf = [s](const char* prefix) { return QLatin1String(prefix) + QString::number(s->card); };

    // ---- A project in the app folder, open, and Home ----------------------------
    steps.push_back([&r, s] {
        // A stand-in trash in its own temporary folder: files go there, never
        // to the Recycle Bin.
        const QString trashDir = s->trash->path();
        r.app().setTrashHandler([trashDir](const QString& path, QString* error) {
            const QString target = trashDir + QLatin1Char('/') + QFileInfo(path).fileName();
            QFile::remove(target);
            if (QFile::rename(path, target))
                return true;
            *error = QStringLiteral("stand-in trash failed");
            return false;
        });
        r.app().setAppFolder(s->folder->path());
        r.app().clearRecentFiles();
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    steps.push_back([] {});
    steps.push_back([&r, s, say, file, part] {
        r.check(r.app().bodyCount() == 1, say(QStringLiteral("a box to save")));
        r.check(r.app().saveInAppFolder(part), say(QStringLiteral("saved into the app folder")));
        s->originalSize = QFileInfo(file(part)).size();
        r.check(s->originalSize > 1000, say(QStringLiteral("the project file (with its preview)")), QString::number(s->originalSize));
    });
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("fileMenuButton")), say(QStringLiteral("File menu"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeMenuItem")), say(QStringLiteral("File → Home"))); });
    steps.push_back([] {});
    steps.push_back([] {});
    steps.push_back([&r, s, say, part, touch] {
        r.check(r.app().homeProjects().size() == 1 && itemText(r, QStringLiteral("homeCardName_0")) == part,
                say(QStringLiteral("one card")), cardNames(r));
        if (touch) {
            // A long press opens the card's menu.
            QQuickItem* card = r.findItem(QStringLiteral("homeCard_0"));
            if (card) {
                s->pressAt = card->mapToScene(QPointF(card->width() / 2, card->height() / 3));
                r.touchPress(s->pressAt);
            }
        } else {
            r.check(r.clickItem(QStringLiteral("homeCardMenu_0")), say(QStringLiteral("the card's ⋯ menu")));
        }
        QQuickItem* menuButton = r.findItem(QStringLiteral("homeCardMenu_0"));
        if (touch)
            r.check(menuButton && menuButton->height() >= 44 && menuButton->width() >= 44, say(QStringLiteral("the ⋯ button is 44 px")),
                    menuButton ? r.num(menuButton->width()) + QStringLiteral(" x ") + r.num(menuButton->height()) : QString());
    });
    steps.push_back([] {});
    steps.push_back([] {});
    steps.push_back([] {});
    steps.push_back([&r, s, say, touch] {
        if (touch)
            r.touchRelease(s->pressAt);
        for (const char* entry : {"homeRename_0", "homeDuplicate_0", "homeRemove_0", "homeDelete_0"}) {
            QQuickItem* item = r.findItem(QLatin1String(entry));
            r.check(item && item->isVisible(), say(QStringLiteral("the menu offers ") + QLatin1String(entry)));
            if (touch && item)
                r.check(item->height() >= 44, say(QStringLiteral("a 44 px entry: ") + QLatin1String(entry)), r.num(item->height()));
        }
    });
    steps.push_back([] {});

    // ---- Duplicate ---------------------------------------------------------------
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeDuplicate_0")), say(QStringLiteral("Duplicate"))); });
    steps.push_back([] {});
    steps.push_back([] {});
    steps.push_back([] {});
    steps.push_back([&r, s, say, file, part, copy] {
        r.check(QFileInfo::exists(file(copy)), say(QStringLiteral("the copy's file is next to the project")), file(copy));
        r.check(QFileInfo(file(copy)).size() == s->originalSize && QFileInfo(file(part)).size() == s->originalSize,
                say(QStringLiteral("as many bytes as the original")),
                QString::number(QFileInfo(file(copy)).size()) + QStringLiteral(" / ") + QString::number(s->originalSize));
        r.check(r.app().homeProjects().size() == 2, say(QStringLiteral("two cards")), cardNames(r));
        r.check(itemText(r, QStringLiteral("homeCardName_0")) == copy && itemText(r, QStringLiteral("homeCardName_1")) == part,
                say(QStringLiteral("the copy is the first card")), cardNames(r));
        QQuickItem* preview = r.findItem(QStringLiteral("homeThumbnail_0"));
        r.check(preview && preview->property("status").toInt() == 1 /* Image.Ready */,
                say(QStringLiteral("the copy shows the preview")));
        r.check(r.app().documentTitle() == part, say(QStringLiteral("the open project stays open")), r.app().documentTitle());
    });

    // ---- Rename the copy: taken, unusable, then a new name -------------------------
    steps.push_back([&r, say, menuOf, copy] { r.check(menuOf(copy), say(QStringLiteral("the copy's menu"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeRename_")), say(QStringLiteral("Rename…"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, copy, part] {
        r.check(shown(r, QStringLiteral("homeRenameCard")), say(QStringLiteral("the rename card")));
        r.check(itemText(r, QStringLiteral("homeRenameField")) == copy, say(QStringLiteral("with the current name")),
                itemText(r, QStringLiteral("homeRenameField")));
        QQuickItem* ok = r.findItem(QStringLiteral("homeRenameOk"));
        r.check(ok && !ok->isEnabled(), say(QStringLiteral("Rename waits for a new name")));
        QQuickItem* panel = r.findItem(QStringLiteral("homeRenamePanel"));
        const QRectF box = panel ? panel->mapRectToScene(QRectF(0, 0, panel->width(), panel->height())) : QRectF();
        r.check(panel && box.left() >= 0 && box.right() <= r.window()->width(), say(QStringLiteral("the card fits the window")),
                QStringLiteral("%1..%2 of %3").arg(box.left()).arg(box.right()).arg(r.window()->width()));
        r.type(part); // replaces the selected name: the original's name
    });
    steps.push_back([&r, say, part, where] {
        const QString note = itemText(r, QStringLiteral("homeRenameNote"));
        r.check(note == QStringLiteral("A project named “%1” is there already.").arg(part), say(QStringLiteral("a taken name is refused")), note);
        QQuickItem* ok = r.findItem(QStringLiteral("homeRenameOk"));
        r.check(ok && !ok->isEnabled(), say(QStringLiteral("Rename is off for a taken name")));
        r.screenshot(QStringLiteral("homeprojects_rename_taken_") + where);
        r.key(Qt::Key_A, Qt::ControlModifier);
        r.type(QStringLiteral("a/b"));
    });
    steps.push_back([&r, say] {
        const QString note = itemText(r, QStringLiteral("homeRenameNote"));
        r.check(note.startsWith(QStringLiteral("A name cannot contain")), say(QStringLiteral("characters files cannot have are refused")), note);
        QQuickItem* ok = r.findItem(QStringLiteral("homeRenameOk"));
        r.check(ok && !ok->isEnabled(), say(QStringLiteral("Rename is off for them")));
        r.key(Qt::Key_A, Qt::ControlModifier);
        r.key(Qt::Key_Backspace);
    });
    steps.push_back([&r, say, spare] {
        const QString note = itemText(r, QStringLiteral("homeRenameNote"));
        r.check(note == QStringLiteral("Type a name for the project."), say(QStringLiteral("an empty name is refused")), note);
        r.type(spare);
    });
    steps.push_back([&r, say, press] {
        const QString note = itemText(r, QStringLiteral("homeRenameNote"));
        r.check(note == QStringLiteral("The project's file gets the new name."), say(QStringLiteral("a free name is fine")), note);
        r.check(press(QStringLiteral("homeRenameOk")), say(QStringLiteral("Rename")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, file, copy, spare, part, where] {
        r.check(!shown(r, QStringLiteral("homeRenameCard")), say(QStringLiteral("the rename card closes")));
        r.check(QFileInfo::exists(file(spare)) && !QFileInfo::exists(file(copy)), say(QStringLiteral("the file has the new name, the old one is gone")));
        r.check(itemText(r, QStringLiteral("homeCardName_0")) == spare, say(QStringLiteral("the card shows the new name")), cardNames(r));
        r.check(r.app().homeProjects().size() == 2, say(QStringLiteral("still two cards")), cardNames(r));
        r.check(r.app().documentTitle() == part, say(QStringLiteral("the open project is another one")));
        r.screenshot(QStringLiteral("homeprojects_renamed_") + where);
    });

    // ---- Rename the open project: the window's title follows --------------------------
    steps.push_back([&r, say, menuOf, part] { r.check(menuOf(part), say(QStringLiteral("the open project's menu"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeRename_")), say(QStringLiteral("Rename… (open project)"))); });
    steps.push_back([] {});
    steps.push_back([&r, wall] { r.type(wall); });
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeRenameOk")), say(QStringLiteral("Rename (open project)"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, file, part, wall] {
        r.check(r.app().documentTitle() == wall, say(QStringLiteral("the document has the new name")), r.app().documentTitle());
        r.check(r.window()->title().contains(wall), say(QStringLiteral("so has the window")), r.window()->title());
        r.check(QFileInfo::exists(file(wall)) && !QFileInfo::exists(file(part)), say(QStringLiteral("the open project's file is renamed")));
        bool listed = false, stale = false;
        for (const QVariant& entry : r.app().recentFiles()) {
            const QString path = entry.toMap().value(QStringLiteral("path")).toString();
            listed = listed || QFileInfo(path) == QFileInfo(file(wall));
            stale = stale || QFileInfo(path) == QFileInfo(file(part));
        }
        r.check(listed && !stale, say(QStringLiteral("Open Recent lists the new name, not the old one")));
        r.check(itemText(r, QStringLiteral("homeCardName_1")) == wall, say(QStringLiteral("its card keeps its place")), cardNames(r));
        // Saving goes to the new name.
        r.check(r.app().saveProject() && !QFileInfo::exists(file(part)), say(QStringLiteral("Save writes the renamed file")));
    });

    // ---- Delete the copy: into the (stand-in) trash -------------------------------------
    steps.push_back([&r, say, menuOf, spare] { r.check(menuOf(spare), say(QStringLiteral("the renamed copy's menu"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeDelete_")), say(QStringLiteral("Delete…"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press, file, spare, touch] {
        r.check(shown(r, QStringLiteral("homeDeleteCard")), say(QStringLiteral("a confirmation card")));
        const QString title = itemText(r, QStringLiteral("homeDeleteTitle"));
        r.check(title == QStringLiteral("Delete “%1”?").arg(spare), say(QStringLiteral("it names the project")), title);
        const QString note = itemText(r, QStringLiteral("homeDeleteNote"));
        r.check(note == QStringLiteral("It goes to ") + r.app().trashName() + QLatin1Char('.'), say(QStringLiteral("and where it goes")), note);
        r.check(QFileInfo::exists(file(spare)), say(QStringLiteral("nothing is deleted before the answer")));
        if (touch) {
            for (const char* button : {"homeDeleteOk", "homeDeleteCancel"}) {
                QQuickItem* item = r.findItem(QLatin1String(button));
                r.check(item && item->height() >= 44, say(QStringLiteral("a 44 px button: ") + QLatin1String(button)),
                        item ? r.num(item->height()) : QString());
            }
        }
        r.screenshot(QStringLiteral("homeprojects_delete_") + (touch ? QStringLiteral("phone") : QStringLiteral("desktop")));
        // Cancel keeps it (tried once, on the phone).
        if (touch)
            r.check(press(QStringLiteral("homeDeleteCancel")), say(QStringLiteral("Cancel")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, menuOf, file, spare, touch] {
        if (!touch)
            return;
        r.check(!shown(r, QStringLiteral("homeDeleteCard")) && QFileInfo::exists(file(spare)), say(QStringLiteral("Cancel keeps the project")));
        r.check(menuOf(spare), say(QStringLiteral("the menu again")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf, touch] {
        if (touch)
            r.check(press(entryOf("homeDelete_")), say(QStringLiteral("Delete… again")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete"))); });
    steps.push_back([] {});
    steps.push_back([&r, s, say, file, spare, wall] {
        r.check(!shown(r, QStringLiteral("homeDeleteCard")), say(QStringLiteral("the card closes")));
        r.check(!QFileInfo::exists(file(spare)), say(QStringLiteral("the file is gone from the folder")));
        const QFileInfo trashed(s->trash->filePath(spare + QStringLiteral(".openshape")));
        r.check(trashed.exists() && trashed.size() == s->originalSize, say(QStringLiteral("it is in the trash, whole")),
                QString::number(trashed.size()));
        r.check(r.app().homeProjects().size() == 1 && itemText(r, QStringLiteral("homeCardName_0")) == wall,
                say(QStringLiteral("its card is gone")), cardNames(r));
    });

    // ---- The trash fails: deleted for good only after a second answer --------------------
    steps.push_back([&r, say, menuOf, wall] {
        r.app().setTrashHandler([](const QString&, QString* error) {
            *error = QStringLiteral("no trash here (stand-in)");
            return false;
        });
        r.check(menuOf(wall), say(QStringLiteral("the open project's menu (duplicate again)")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeDuplicate_")), say(QStringLiteral("Duplicate again"))); });
    steps.push_back([] {});
    steps.push_back([] {});
    steps.push_back([&r, say, menuOf, file, wall] {
        r.check(QFileInfo::exists(file(wall + QStringLiteral(" copy"))), say(QStringLiteral("a second copy")));
        r.check(menuOf(wall + QStringLiteral(" copy")), say(QStringLiteral("its menu")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeDelete_")), say(QStringLiteral("Delete… (no trash)"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete (no trash)"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press, file, wall, where] {
        const QString gone = wall + QStringLiteral(" copy");
        r.check(shown(r, QStringLiteral("homeDeleteCard")), say(QStringLiteral("the card asks again")));
        const QString title = itemText(r, QStringLiteral("homeDeleteTitle"));
        r.check(title == QStringLiteral("Delete “%1” for good?").arg(gone), say(QStringLiteral("for good")), title);
        const QString note = itemText(r, QStringLiteral("homeDeleteNote"));
        r.check(note.contains(QStringLiteral("cannot be undone")), say(QStringLiteral("it says it cannot be undone")), note);
        r.check(QFileInfo::exists(file(gone)), say(QStringLiteral("the file is still there")));
        r.screenshot(QStringLiteral("homeprojects_delete_for_good_") + where);
        r.check(press(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete for Good")));
    });
    steps.push_back([] {});
    steps.push_back([&r, s, say, file, wall] {
        const QString gone = wall + QStringLiteral(" copy");
        r.check(!shown(r, QStringLiteral("homeDeleteCard")), say(QStringLiteral("the card closes (for good)")));
        r.check(!QFileInfo::exists(file(gone)) && !QFileInfo::exists(s->trash->filePath(gone + QStringLiteral(".openshape"))),
                say(QStringLiteral("the file is gone for good")));
        r.check(r.app().homeProjects().size() == 1, say(QStringLiteral("one card left")), cardNames(r));
    });

    // ---- A project outside OpenShape's folder: the trash only ----------------------------
    // Once (the platform decides, not the layout). On the desktop without an
    // app folder, as the Windows app runs.
    if (!touch) {
        steps.push_back([&r, s, say, file, wall] {
            const QString path = s->outside->filePath(QStringLiteral("Elsewhere.openshape"));
            r.check(QFile::copy(file(wall), path), say(QStringLiteral("a project outside the app folder")));
            r.app().setAppFolder({});
            r.check(r.app().openRecent(path), say(QStringLiteral("opened, so it is a recent project")));
            r.app().setHomeVisible(true);
        });
        steps.push_back([] {});
        steps.push_back([&r, say, menuOf] {
            const QVariantMap first = r.app().homeProjects().value(0).toMap();
            r.check(first.value(QStringLiteral("name")) == QStringLiteral("Elsewhere"), say(QStringLiteral("its card is first")), cardNames(r));
            r.check(first.value(QStringLiteral("deletable")).toBool(), say(QStringLiteral("the desktop offers Delete… for a recent project")));
            r.check(menuOf(QStringLiteral("Elsewhere")), say(QStringLiteral("its menu")));
        });
        steps.push_back([] {});
        steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeDelete_")), say(QStringLiteral("Delete… (outside)"))); });
        steps.push_back([] {});
        steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete (outside, no trash)"))); });
        steps.push_back([] {});
        steps.push_back([&r, s, say, file, wall] {
            const QString path = s->outside->filePath(QStringLiteral("Elsewhere.openshape"));
            r.check(QFileInfo::exists(path), say(QStringLiteral("outside OpenShape's folder a project is never deleted for good")));
            r.check(!shown(r, QStringLiteral("homeDeleteCard")), say(QStringLiteral("the card closes without asking again")));
            r.check(r.app().documentTitle() == QStringLiteral("Elsewhere"), say(QStringLiteral("and it stays open")));
            // Back to the app folder's project for the next part.
            r.app().removeRecentFile(path);
            r.app().setAppFolder(s->folder->path());
            r.check(r.app().openRecent(file(wall)), say(QStringLiteral("reopen the app folder's project")));
            r.app().setHomeVisible(true);
        });
        steps.push_back([] {});
    }

    // ---- Delete the open project: an empty document, Home stays --------------------------
    steps.push_back([&r, say, menuOf, wall] {
        r.check(r.app().documentTitle() == wall, say(QStringLiteral("the open project")), r.app().documentTitle());
        r.check(r.app().homeProjects().size() == 1, say(QStringLiteral("one card")), cardNames(r));
        r.check(menuOf(wall), say(QStringLiteral("the open project's menu (delete)")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, press, entryOf] { r.check(press(entryOf("homeDelete_")), say(QStringLiteral("Delete… (open project)"))); });
    steps.push_back([] {});
    steps.push_back([&r, say, press] {
        const QString note = itemText(r, QStringLiteral("homeDeleteNote"));
        r.check(note.contains(QStringLiteral("It is open: it closes.")), say(QStringLiteral("the card says it is open")), note);
        r.check(press(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete (open project, no trash)")));
    });
    steps.push_back([] {});
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete for Good (open project)"))); });
    steps.push_back([] {});
    steps.push_back([&r, s, say, file, wall] {
        r.check(!QFileInfo::exists(file(wall)), say(QStringLiteral("the open project's file is gone")));
        r.check(r.app().documentTitle() == QStringLiteral("Untitled") && r.app().bodyCount() == 0,
                say(QStringLiteral("an empty document takes its place")), r.app().documentTitle());
        r.check(r.app().homeVisible(), say(QStringLiteral("Home stays")));
        r.check(r.app().homeProjects().isEmpty() && shown(r, QStringLiteral("homeNew")), say(QStringLiteral("no cards left")), cardNames(r));
        r.check(!shown(r, QStringLiteral("homeBack")), say(QStringLiteral("nothing to go back to")));
        const QStringList left = QDir(s->folder->path()).entryList({QStringLiteral("*.openshape")}, QDir::Files);
        r.check(left.isEmpty(), say(QStringLiteral("the app folder is empty again")), left.join(QStringLiteral(", ")));
    });
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<HomeProjectsState>();
    std::vector<AcceptanceRunner::Step> all;
    all.push_back([&r, s] {
        s->folder = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-homeprojects-XXXXXX"));
        s->trash = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-homeprojects-trash-XXXXXX"));
        s->outside = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-homeprojects-outside-XXXXXX"));
        r.check(s->folder->isValid() && s->trash->isValid() && s->outside->isValid(), "home projects: temporary folders");
    });
    addPass(all, r, s, QStringLiteral("desktop"), false);
    // An iPhone held upright, used by touch.
    all.push_back([&r] {
        r.app().setHomeVisible(false);
        r.resizeWindow(402, 874);
        r.app().setTouchMode(true);
    });
    all.push_back([] {});
    all.push_back([] {});
    addPass(all, r, s, QStringLiteral("phone"), true);
    all.push_back([&r, s] {
        r.app().setTouchMode(false);
        r.app().setHomeVisible(false);
        r.app().clearRecentFiles();
        r.app().setAppFolder({});
        r.app().setTrashHandler(ui::AppController::systemTrashHandler());
        s->folder.reset();
        s->trash.reset();
        s->outside.reset();
    });
    return all;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("homeprojects"), 108, steps});

} // namespace
} // namespace os::app
