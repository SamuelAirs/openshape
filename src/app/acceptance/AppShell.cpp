// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The app shell: recovery copies and restoring after a real (simulated)
// crash, File → Open Recent, and File → Preferences. Automated runs keep
// settings and recovery copies in a throw-away folder; these scenarios check
// that and use it.

#include "app/AcceptanceRunner.h"
#include "core/Uuid.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"
#include "io/Recovery.h"
#include "ui/AppController.h"
#include "ui/AppSettings.h"
#include "ui/RecoverySession.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLockFile>
#if QT_CONFIG(process)
#include <QtCore/QProcess>
#endif
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QSysInfo>
#include <QtCore/QUrl>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

// Recovery copies are written this long after the last edit, plus margin
// (steps are 160 ms apart).
constexpr int kDebounceSteps = ui::kRecoveryDebounceMs / 160 + 4;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

std::unique_ptr<doc::Document> loadCopy(const QString& file)
{
    auto loaded = io::loadProject(std::filesystem::path(file.toStdWString()));
    return loaded ? std::move(loaded.value()) : nullptr;
}

double volumeOf(const doc::Document* d)
{
    return d && !d->bodies().empty() ? geom::volume(d->bodies().front()->shape()) : -1.0;
}

std::unique_ptr<doc::Document> cube(double size)
{
    auto d = std::make_unique<doc::Document>();
    auto body = std::make_unique<doc::Body>();
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {size, size, size};
    body->insertFeature(std::move(box), 0);
    d->addBody(std::move(body));
    return d;
}

QString path(const std::filesystem::path& p)
{
    return QString::fromStdWString(p.wstring());
}

// What a crashed run leaves: its copy, sidecar and a lock whose process is gone.
std::string crashedCopy(ui::RecoverySession& recovery, double size, const std::string& originalPath, const std::string& title)
{
    const std::string session = Uuid::generate().toString();
    (void)recovery.store().write(session, *cube(size), {originalPath, title, 0, "0.1.0"});
    QFile lock(path(recovery.store().lockFile(session)));
    if (lock.open(QIODevice::WriteOnly | QIODevice::Truncate))
        lock.write("2147483644\nOpenShape\n" + QSysInfo::machineHostName().toUtf8() + "\n");
    return session;
}

QString recoveryItemSession(const ui::AppController& app, int index)
{
    const QVariantList items = app.recoveryItems();
    return index < items.size() ? items[index].toMap().value(QStringLiteral("session")).toString() : QString();
}

// ---- Recovery copies -------------------------------------------------------------------

struct RecoveryState {
    QString outputDir;
    std::string crashed;  // the simulated crash's session
    std::string original; // a crashed copy that had been saved before
    std::string first, second, third, later;
    std::unique_ptr<QLockFile> liveLock; // "another running OpenShape"
    std::string live;
};

Steps recoverySteps(AcceptanceRunner& r)
{
    auto s = std::make_shared<RecoveryState>();
    auto& app = r.app();
    Steps steps;
    steps.push_back([&r, &app, s] {
        // Automated runs never touch the user's data.
        // (Both come from QDir::tempPath(), so plain prefixes compare.)
        const QString temp = QDir::fromNativeSeparators(QDir::tempPath()) + QLatin1Char('/');
        const QString settings = QDir::fromNativeSeparators(QSettings().fileName());
        r.check(QSettings().format() == QSettings::IniFormat && settings.startsWith(temp, Qt::CaseInsensitive),
                "settings go to a throw-away INI file", settings);
        ui::RecoverySession* recovery = app.recoverySession();
        r.check(recovery && recovery->isLocked(), "this run has a locked recovery session");
        if (!recovery)
            return;
        const QString dir = QDir::fromNativeSeparators(recovery->directory());
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        r.check(dir.startsWith(temp, Qt::CaseInsensitive) && !dir.startsWith(appData + QLatin1Char('/'), Qt::CaseInsensitive),
                "recovery copies go to a throw-away folder", dir);
        r.check(app.recoveryItems().isEmpty(), "no restore prompt in automated runs");
        s->outputDir = QFileInfo(recovery->directory()).absolutePath();
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.dirty() && app.recoveryCopyFile().isEmpty(), "an edit: no copy yet, edits settle first");
    });
    wait(steps, kDebounceSteps);
    steps.push_back([&r, &app] {
        const QString file = app.recoveryCopyFile();
        r.check(!file.isEmpty(), "a few seconds after the edit, a recovery copy exists", file);
        const auto copy = loadCopy(file);
        r.check(copy && std::abs(volumeOf(copy.get()) - 8000.0) < 1e-6, "the copy loads with the same volume",
                AcceptanceRunner::num(volumeOf(copy.get())));
        // Undo back to the saved (empty) state: nothing to recover.
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.check(!app.dirty() && app.recoveryCopyFile().isEmpty(), "undo to the saved state removes the copy");
        r.key(Qt::Key_Y, Qt::ControlModifier);
    });
    wait(steps, kDebounceSteps);
    steps.push_back([&r, &app, s] {
        r.check(!app.recoveryCopyFile().isEmpty(), "redo: unsaved again, copied again");
        const QString saved = s->outputDir + QStringLiteral("/recovery_saved.openshape");
        QFile::remove(saved);
        r.check(app.saveProjectAs(QUrl::fromLocalFile(saved)), "Save As");
        r.check(app.recoveryCopyFile().isEmpty(), "saving removes the recovery copy");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.recoveryCopyFile().isEmpty(), "an edit after saving: not copied at once");
        // Switching to another app (or the iPad home screen) copies right away.
        if (auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
            emit gui->applicationStateChanged(Qt::ApplicationInactive);
        const auto copy = loadCopy(app.recoveryCopyFile());
        r.check(copy && copy->bodies().size() == 2, "leaving the app copies the unsaved work immediately");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu"); });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("newMenuItem")), "File → New with unsaved changes"); });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        auto* question = r.findItem(QStringLiteral("unsavedDialog"));
        r.check(question && question->isVisible() && app.bodyCount() == 2, "it asks about saving first");
        r.screenshot(QStringLiteral("recovery_unsaved_question"));
        r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "Don't Save button");
        r.check(app.bodyCount() == 0 && !app.dirty(), "Don't Save: a new document");
        r.check(app.recoveryCopyFile().isEmpty(), "a confirmed discard removes the copy");
    });
    // A real crash: another OpenShape adds a box, writes its copy and crashes.
    // (No second process on iPadOS: there the crashed run's files are made here.)
    steps.push_back([&r, &app, s] {
        ui::RecoverySession* recovery = app.recoverySession();
        if (!recovery)
            return;
#if QT_CONFIG(process)
        const QString crashDir = s->outputDir + QStringLiteral("/crashed-run");
        QDir(crashDir).removeRecursively();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--data-dir"), crashDir, QStringLiteral("--simulate-crash")});
        const bool finished = child.waitForFinished(90000);
        r.check(finished && child.exitStatus() == QProcess::CrashExit, "the other OpenShape crashed",
                QStringLiteral("exit code 0x%1").arg(uint(child.exitCode()), 8, 16, QLatin1Char('0')));
        QFile log(crashDir + QStringLiteral("/logs/openshape.log"));
        const QByteArray logText = log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
        r.check(logText.contains("OpenShape closed unexpectedly"), "the crash is in its log",
                QString::fromUtf8(logText.right(200)));
        // Its recovery folder: move the copy, sidecar and (dead) lock into ours.
        const QDir from(crashDir + QStringLiteral("/recovery"));
        const QStringList copies = from.entryList({QStringLiteral("*.openshape")}, QDir::Files);
        r.check(copies.size() == 1, "the crash left its recovery copy", from.entryList(QDir::Files).join(QLatin1Char(' ')));
        if (copies.size() != 1)
            return;
        s->crashed = copies.front().chopped(10).toStdString();
        for (const char* ext : {".openshape", ".json", ".lock"}) {
            const QString name = QString::fromStdString(s->crashed) + QLatin1String(ext);
            QFile::copy(from.filePath(name), recovery->directory() + QLatin1Char('/') + name);
        }
        r.check(QFile::exists(path(recovery->store().lockFile(s->crashed))), "with the dead run's lock file");
#else
        s->crashed = crashedCopy(*recovery, 20, "", "");
#endif
        // Another running OpenShape's copy must not be offered.
        s->live = Uuid::generate().toString();
        (void)recovery->store().write(s->live, *cube(5), {"", "Live", 0, "0.1.0"});
        s->liveLock = std::make_unique<QLockFile>(path(recovery->store().lockFile(s->live)));
        r.check(s->liveLock->tryLock(0), "another instance's lock is held");
        app.checkForRecovery(); // what startup does
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        auto* overlay = r.findItem(QStringLiteral("recoveryOverlay"));
        r.check(overlay && overlay->isVisible(), "the restore prompt appears");
        r.check(app.recoveryItems().size() == 1 && recoveryItemSession(app, 0).toStdString() == s->crashed,
                "it offers the crashed run's work only (not the running one's)", QString::number(app.recoveryItems().size()));
        r.screenshot(QStringLiteral("recovery_prompt"));
        // The window's shortcuts wait until the question is answered.
        r.key(Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));
        auto* preferences = r.findItem(QStringLiteral("preferencesOverlay"));
        r.check(preferences && !preferences->isVisible(),
                "Ctrl+, does not open Preferences behind the restore prompt");
        r.check(r.clickItem(QStringLiteral("recoveryRestore_") + QString::fromStdString(s->crashed)), "Restore button");
    });
    wait(steps, 3);
    steps.push_back([&r, &app, s] {
        auto* overlay = r.findItem(QStringLiteral("recoveryOverlay"));
        r.check(overlay && !overlay->isVisible(), "the prompt closes");
        r.check(app.bodyCount() == 1 && std::abs(r.bodyVolume() - 8000.0) < 1e-6, "the crashed run's box is back",
                AcceptanceRunner::num(r.bodyVolume()));
        r.check(app.dirty() && !app.hasProjectPath() && app.documentTitle() == QStringLiteral("Untitled"),
                "restored as an unsaved, untitled document", app.documentTitle());
        ui::RecoverySession* recovery = app.recoverySession();
        r.check(recovery && !recovery->store().exists(s->crashed) && !QFile::exists(path(recovery->store().lockFile(s->crashed))),
                "the crashed run's files are gone");
        const auto copy = loadCopy(app.recoveryCopyFile());
        r.check(copy && std::abs(volumeOf(copy.get()) - 8000.0) < 1e-6, "its copy stays (now this run's) until saved");
        const QString saved = s->outputDir + QStringLiteral("/recovery_restored.openshape");
        QFile::remove(saved);
        r.check(app.saveProjectAs(QUrl::fromLocalFile(saved)) && app.recoveryCopyFile().isEmpty(), "saving it removes the copy");
        // A crashed copy of a document that had a file: Save writes there.
        const QString original = s->outputDir + QStringLiteral("/recovery_original.openshape");
        QFile::remove(original);
        if (recovery)
            s->original = crashedCopy(*recovery, 30, original.toStdString(), "recovery_original");
        app.checkForRecovery();
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("recoveryRestore_") + QString::fromStdString(s->original)), "Restore (a saved document)");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        r.check(app.hasProjectPath() && app.documentTitle() == QStringLiteral("recovery_original") && app.dirty(),
                "it remembers its file and is unsaved", app.documentTitle());
        r.key(Qt::Key_S, Qt::ControlModifier);
        const QString original = s->outputDir + QStringLiteral("/recovery_original.openshape");
        const auto saved = loadCopy(original);
        r.check(saved && std::abs(volumeOf(saved.get()) - 27000.0) < 1e-6, "Ctrl+S saves it to its own file",
                AcceptanceRunner::num(volumeOf(saved.get())));
        r.check(!app.dirty() && app.recoveryCopyFile().isEmpty(), "saved: no recovery copy");
        // Three more crashed copies: Discard one, then Discard all.
        if (ui::RecoverySession* recovery = app.recoverySession()) {
            s->first = crashedCopy(*recovery, 5, "", "");
            s->second = crashedCopy(*recovery, 6, "", "");
            s->third = crashedCopy(*recovery, 7, "", "");
        }
        app.checkForRecovery();
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        r.check(app.recoveryItems().size() == 3, "three crashed copies offered", QString::number(app.recoveryItems().size()));
        r.check(r.clickItem(QStringLiteral("recoveryDiscard_") + QString::fromStdString(s->first)), "Discard button");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        ui::RecoverySession* recovery = app.recoverySession();
        r.check(app.recoveryItems().size() == 2 && recovery && !recovery->store().exists(s->first),
                "Discard deletes that copy", QString::number(app.recoveryItems().size()));
        r.check(app.bodyCount() == 1 && std::abs(r.bodyVolume() - 27000.0) < 1e-6, "the open document is untouched");
        r.check(r.clickItem(QStringLiteral("recoveryDiscardAll")), "Discard all button");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        ui::RecoverySession* recovery = app.recoverySession();
        auto* overlay = r.findItem(QStringLiteral("recoveryOverlay"));
        r.check(app.recoveryItems().isEmpty() && overlay && !overlay->isVisible(), "Discard all closes the prompt");
        r.check(recovery && !recovery->store().exists(s->second) && !recovery->store().exists(s->third), "and deletes the copies");
        if (recovery)
            s->later = crashedCopy(*recovery, 8, "", "later");
        app.checkForRecovery();
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("recoveryLater")), "Decide later button"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        ui::RecoverySession* recovery = app.recoverySession();
        r.check(app.recoveryItems().isEmpty() && recovery && recovery->store().exists(s->later), "Decide later keeps the copy");
        app.checkForRecovery();
        r.check(recoveryItemSession(app, 0).toStdString() == s->later, "and it is offered again next time");
        app.discardAllRecovery();
        r.check(recovery && !recovery->store().exists(s->later) && recovery->store().exists(s->live),
                "cleaned up; the running instance's copy was never touched");
        s->liveLock.reset();
        if (recovery)
            (void)recovery->store().remove(s->live);
    });
#if QT_CONFIG(process)
    // The app is ended with unsaved work the user never discarded (iPadOS
    // ending it, Windows logging off): not a crash, but the work must stay.
    steps.push_back([&r, s] {
        const QString quitDir = s->outputDir + QStringLiteral("/quit-run");
        QDir(quitDir).removeRecursively();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--data-dir"), quitDir, QStringLiteral("--simulate-quit")});
        const bool finished = child.waitForFinished(90000);
        r.check(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                "another OpenShape quits with an unsaved box, without being asked", QString::number(child.exitCode()));
        const QDir dir(quitDir + QStringLiteral("/recovery"));
        r.check(dir.entryList({QStringLiteral("*.lock")}, QDir::Files).isEmpty(), "it released its lock",
                dir.entryList(QDir::Files).join(QLatin1Char(' ')));
        QFile log(quitDir + QStringLiteral("/logs/openshape.log"));
        const QByteArray logText = log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
        r.check(logText.contains("recovery copy kept for the next start"), "its log says the copy is kept",
                QString::fromUtf8(logText.right(300)).simplified());
        ui::RecoverySession next(dir.path());
        const auto orphans = next.findOrphans();
        const auto copy = orphans.size() == 1 ? loadCopy(path(orphans[0].projectFile)) : nullptr;
        r.check(copy && std::abs(volumeOf(copy.get()) - 8000.0) < 1e-6, "its unsaved box is offered at the next start",
                QString::number(orphans.size()));
    });
#endif
    return steps;
}

// Whether a Menu (a popup, found by objectName) is open.
bool menuOpen(AcceptanceRunner& r, const char* name)
{
    QObject* menu = r.window()->findChild<QObject*>(QString::fromLatin1(name));
    return menu && menu->property("visible").toBool();
}

// Lets the first close request through (the window asks about unsaved
// changes) and swallows the later ones (the one "Don't Save" makes), so the
// run can check what Don't Save did without ending.
class CloseCatcher : public QObject {
public:
    int closes = 0;
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Close)
            return QObject::eventFilter(watched, event);
        return ++closes > 1;
    }
};

// ---- Open Recent ------------------------------------------------------------------------

struct RecentState {
    QString a, b;
};

Steps recentSteps(AcceptanceRunner& r)
{
    auto s = std::make_shared<RecentState>();
    auto& app = r.app();
    Steps steps;
    steps.push_back([&r, &app, s] {
        const QString dir = QFileInfo(app.recoverySession() ? app.recoverySession()->directory() : QDir::tempPath()).absolutePath();
        s->a = dir + QStringLiteral("/recent_a.openshape");
        s->b = dir + QStringLiteral("/recent_b.openshape");
        app.clearRecentFiles();
        r.check(app.recentFiles().isEmpty(), "no recent files at first");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.saveProjectAs(QUrl::fromLocalFile(s->a)), "save A");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.saveProjectAs(QUrl::fromLocalFile(s->b)), "save B (two boxes)");
        const QVariantList recent = app.recentFiles();
        r.check(recent.size() == 2 && recent[0].toMap()[QStringLiteral("name")] == QStringLiteral("recent_b")
                    && recent[1].toMap()[QStringLiteral("name")] == QStringLiteral("recent_a"),
                "saving adds them, most recent first", QString::number(recent.size()));
        app.newDocument();
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("openRecentMenuItem")), "Open Recent"); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.screenshot(QStringLiteral("recent_menu"));
        r.check(r.clickItem(QStringLiteral("recentFile_1")), "the second recent file (A)");
    });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.check(app.documentTitle() == QStringLiteral("recent_a") && app.bodyCount() == 1, "it opens A", app.documentTitle());
        const QVariantList recent = app.recentFiles();
        r.check(!recent.isEmpty() && recent[0].toMap()[QStringLiteral("name")] == QStringLiteral("recent_a"),
                "opening moves it to the top");
        // Unsaved changes: Open Recent asks first, like Open.
        QQuickItem* focus = r.window()->activeFocusItem();
        r.check(focus && focus->objectName() == QStringLiteral("viewport"), "after the menu, keys go to the view",
                focus ? QString::fromLatin1(focus->metaObject()->className()) + QLatin1Char(' ') + focus->objectName()
                      : QStringLiteral("nothing"));
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.dirty() && app.bodyCount() == 2, "B adds a box: unsaved changes");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (unsaved changes)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("openRecentMenuItem")), "Open Recent (unsaved changes)"); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("recentFile_1")), "recent file B with unsaved changes"); });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        auto* question = r.findItem(QStringLiteral("unsavedDialog"));
        r.check(question && question->isVisible(), "the unsaved-changes question appears");
        r.check(app.documentTitle() == QStringLiteral("recent_a") && app.bodyCount() == 2, "and B is not opened yet");
        // Shortcuts wait while it asks: no undo behind it, no second question.
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.key(Qt::Key_N, Qt::ControlModifier);
        r.check(question && question->isVisible() && app.bodyCount() == 2 && app.documentTitle() == QStringLiteral("recent_a"),
                "Ctrl+Z and Ctrl+N do nothing behind the question", QString::number(app.bodyCount()));
        r.check(r.clickItem(QStringLiteral("unsavedCancel")), "Cancel button");
        r.check(question && !question->isVisible() && app.documentTitle() == QStringLiteral("recent_a") && app.dirty(),
                "Cancel: A stays open, unsaved");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (save first)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("openRecentMenuItem")), "Open Recent (save first)"); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("recentFile_1")), "recent file B again"); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.key(Qt::Key_N, Qt::ControlModifier); // must not replace "open B"
        r.check(r.clickItem(QStringLiteral("unsavedSave")), "Save button");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        const auto savedA = loadCopy(s->a);
        r.check(savedA && savedA->bodies().size() == 2, "Save wrote A (now two boxes)");
        r.check(app.documentTitle() == QStringLiteral("recent_b") && !app.dirty(), "then B opened", app.documentTitle());
        // A file deleted while OpenShape runs (e.g. in Explorer) drops out of the menu.
        app.newDocument();
        QFile::remove(s->b);
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (B was deleted)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("openRecentMenuItem")), "Open Recent (B was deleted)"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        auto* first = r.findItem(QStringLiteral("recentFile_0"));
        auto* second = r.findItem(QStringLiteral("recentFile_1"));
        r.check(first && first->isVisible() && first->property("text").toString().startsWith(QStringLiteral("recent_a"))
                    && !(second && second->isVisible()),
                "the menu no longer lists the deleted file",
                first ? first->property("text").toString() : QStringLiteral("no entries"));
        r.screenshot(QStringLiteral("recent_menu_after_delete"));
        const QVariantList recent = app.recentFiles();
        r.check(recent.size() == 1 && recent[0].toMap()[QStringLiteral("path")].toString() == QFileInfo(s->a).absoluteFilePath(),
                "and the list has only A", QString::number(recent.size()));
        r.key(Qt::Key_Escape);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(!menuOpen(r, "openRecentMenu") && menuOpen(r, "fileMenu"), "Esc closes the sub-menu",
                QStringLiteral("file menu open: %1").arg(menuOpen(r, "fileMenu")));
        r.key(Qt::Key_Escape);
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        r.check(r.window()->findChild<QObject*>(QStringLiteral("fileMenu")) && !menuOpen(r, "fileMenu"),
                "Esc again closes the File menu");
        QQuickItem* focus = r.window()->activeFocusItem();
        r.check(focus && focus->objectName() == QStringLiteral("viewport"), "and keys go to the view",
                focus ? QString::fromLatin1(focus->metaObject()->className()) + QLatin1Char(' ') + focus->objectName()
                      : QStringLiteral("nothing"));
        // Opening from the command line (or a double-clicked file) goes through openProject.
        app.newDocument();
        r.check(app.saveProjectAs(QUrl::fromLocalFile(s->b)), "save B again");
        app.newDocument();
        r.check(app.openProject(QUrl::fromLocalFile(s->a)), "open A as from the command line");
        r.check(app.recentFiles().value(0).toMap()[QStringLiteral("name")] == QStringLiteral("recent_a"), "it is listed first");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (clear)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("openRecentMenuItem")), "Open Recent (clear)"); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("clearRecentFiles")), "Clear Recent"); });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.check(app.recentFiles().isEmpty(), "the list is empty");
        auto* item = r.findItem(QStringLiteral("openRecentMenuItem"));
        r.check(item && !item->isEnabled(), "Open Recent is disabled when empty");
    });
    return steps;
}

// ---- Preferences ------------------------------------------------------------------------

Steps preferencesSteps(AcceptanceRunner& r)
{
    auto& app = r.app();
    auto catcher = std::make_shared<CloseCatcher*>(nullptr);
    Steps steps;
    auto overlayVisible = [&r] {
        auto* overlay = r.findItem(QStringLiteral("preferencesOverlay"));
        return overlay && overlay->isVisible();
    };
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu"); });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("preferencesMenuItem")), "Preferences… in the File menu"); });
    wait(steps, 2);
    steps.push_back([&r, &app, overlayVisible] {
        r.check(overlayVisible(), "the Preferences panel opens");
        r.screenshot(QStringLiteral("preferences"));
        r.check(r.clickItem(QStringLiteral("prefUnit_in")), "Inches for new documents");
        r.check(app.defaultUnit() == QStringLiteral("in"), "default unit is inches", app.defaultUnit());
        r.check(app.displayUnit() == QStringLiteral("in"), "the untouched new document follows at once", app.displayUnit());
        QSettings settings;
        r.check(ui::loadPreferences(settings).defaultUnit == LengthUnit::Inch, "and it is remembered");
        r.check(r.clickItem(QStringLiteral("preferencesClose")), "Close button");
        r.check(!overlayVisible(), "the panel closes");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (New)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("newMenuItem")), "File → New"); });
    wait(steps, 2);
    steps.push_back([&r] {
        auto* question = r.findItem(QStringLiteral("unsavedDialog"));
        r.check(question && question->isVisible(), "New asks about the unsaved box");
        r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "Don't Save");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, overlayVisible] {
        r.check(app.bodyCount() == 0 && app.displayUnit() == QStringLiteral("in"), "a new document is in inches", app.displayUnit());
        r.key(Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));
        r.check(overlayVisible(), "Ctrl+, opens Preferences");
        r.check(r.clickItem(QStringLiteral("prefUnit_mm")), "Millimeters for new documents");
        r.check(app.defaultUnit() == QStringLiteral("mm") && app.displayUnit() == QStringLiteral("mm"), "back to millimeters");
        r.check(r.clickItem(QStringLiteral("prefGridSnap_false")), "Free (no grid snap)");
        r.check(!app.sketchGridSnap() && !app.interaction().sketchGridSnap(), "grid snapping is off");
        r.key(Qt::Key_Escape);
        r.check(!overlayVisible(), "Esc closes Preferences");
        r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    });
    wait(steps, 4);
    steps.push_back([&r, &app] {
        // A line from an off-grid point: it starts exactly where clicked.
        auto* session = app.interaction().sketchSession();
        r.check(session && !session->gridSnap(), "a new sketch does not snap to the grid");
        r.key(Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
        const Vec3 a{7.37, 4.21, 0};
        r.click(r.screenPoint(a.x, a.y, a.z));
        r.click(r.screenPoint(21.53, 17.87, 0));
        r.key(Qt::Key_Escape);
        bool exact = false;
        if (session)
            for (const auto& [id, p] : session->sketch().points())
                exact = exact || (std::abs(p.position.x - a.x) < 0.25 && std::abs(p.position.y - a.y) < 0.25
                                  && std::abs(p.position.x * 10 - std::round(p.position.x * 10)) > 1e-6);
        r.check(exact, "the point is where clicked, off the grid");
        app.finishSketch();
        r.key(Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));
        r.check(r.clickItem(QStringLiteral("prefGridSnap_true")), "Snap to grid");
        r.check(app.sketchGridSnap() && app.interaction().sketchGridSnap(), "grid snapping is on again");
        r.check(r.clickItem(QStringLiteral("prefRecovery_0")), "Recovery copies: Off");
        r.check(app.recoveryInterval() == 0, "recovery copies off");
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, kDebounceSteps);
    steps.push_back([&r, &app] {
        r.check(app.dirty() && app.recoveryCopyFile().isEmpty(), "Off: no recovery copy after an edit");
        r.key(Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));
        r.check(r.clickItem(QStringLiteral("prefRecovery_30")), "Recovery copies: 30 s");
        r.check(app.recoveryInterval() == 30, "every 30 s", QString::number(app.recoveryInterval()));
    });
    wait(steps, kDebounceSteps);
    steps.push_back([&r, &app] {
        r.check(!app.recoveryCopyFile().isEmpty(), "on again: the unsaved work is copied");
        r.check(r.clickItem(QStringLiteral("prefRecovery_300")), "Recovery copies: 5 min");
        r.check(app.recoveryInterval() == 300, "every 5 min");
        r.check(r.clickItem(QStringLiteral("prefRecovery_60")), "Recovery copies: 1 min");
        QSettings settings;
        const ui::Preferences stored = ui::loadPreferences(settings);
        r.check(stored.recoveryIntervalSeconds == 60 && stored.sketchGridSnap && stored.defaultUnit == LengthUnit::Millimeter,
                "all of it is remembered");
        r.screenshot(QStringLiteral("preferences_after"));
        r.key(Qt::Key_Escape);
    });
    // Closing the window with unsaved changes asks too; Cancel keeps it open.
    steps.push_back([&r, &app] {
        r.check(app.dirty(), "unsaved changes before closing");
        r.window()->close();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        auto* question = r.findItem(QStringLiteral("unsavedDialog"));
        r.check(r.window()->isVisible() && question && question->isVisible(), "closing the window asks first");
        r.check(r.clickItem(QStringLiteral("unsavedCancel")), "Cancel keeps the window open");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, catcher] {
        r.check(r.window()->isVisible(), "the window is still open");
        // Don't Save when closing lets go of the work: no recovery copy is
        // kept (the window's close is held back here so the run goes on).
        r.check(app.dirty() && !app.recoveryCopyFile().isEmpty(), "a recovery copy of the unsaved work exists");
        *catcher = new CloseCatcher;
        r.window()->installEventFilter(*catcher);
        r.window()->close();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        auto* question = r.findItem(QStringLiteral("unsavedDialog"));
        r.check(question && question->isVisible(), "closing asks again");
        r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "Don't Save (closing)");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, catcher] {
        CloseCatcher* c = *catcher;
        r.check(c && c->closes == 2, "Don't Save closes the window", QString::number(c ? c->closes : -1));
        r.check(app.recoveryCopyFile().isEmpty(), "and removes the recovery copy (nothing is offered at the next start)");
        if (c) {
            r.window()->removeEventFilter(c);
            delete c;
            *catcher = nullptr;
        }
        r.window()->setProperty("closeConfirmed", false); // the window stays for the next scenario
    });
    return steps;
}

const bool registeredRecovery = registerAcceptanceScenario({QStringLiteral("recovery"), 50, recoverySteps});
const bool registeredRecent = registerAcceptanceScenario({QStringLiteral("recent"), 51, recentSteps});
const bool registeredPreferences = registerAcceptanceScenario({QStringLiteral("preferences"), 52, preferencesSteps});

} // namespace
} // namespace os::app
