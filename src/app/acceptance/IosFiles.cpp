// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Files on an iPhone or iPad, in the simulated iOS mode (an app folder, as
// with --app-folder):
// - "share": File → Export STL / 3MF / STEP write into Exports and then
//   open the share sheet with the file; File → Share Project… saves first
//   (asking for a name for a new project) and shares the project file. The
//   native sheet cannot be clicked: a stub share handler takes the request
//   (the file, and the File button the iPad's popover points at) and
//   answers as the sheet would (sent, closed, failed).
// - "openin": files handed over by other apps ("Open in OpenShape", sharing
//   to OpenShape from the Files app or Mail) arrive through
//   QWindowSystemInterface::handleFileOpenEvent, the call Qt's iOS scene
//   delegate makes for UIKit's openURLContexts: a real project from
//   elsewhere is copied into OpenShape's folder and opened, a real STEP file
//   from Mail's Inbox becomes a new project, "Save changes?" is asked first,
//   and a file OpenShape cannot use is refused plainly. Also the Open and
//   Import STEP pickers with files outside OpenShape's folder.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "document/Feature.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "io/ProjectFile.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

void append(Steps& steps, Steps more)
{
    for (auto& step : more)
        steps.push_back(std::move(step));
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

QString toastText(AcceptanceRunner& r)
{
    QQuickItem* toast = r.findItem(QStringLiteral("toast"));
    return toast && toast->isVisible() ? toast->property("text").toString() : QString();
}

// File → <item> (the menu opens first).
Steps fileMenu(AcceptanceRunner& r, const QString& item, const QString& what)
{
    return {
        [&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu"); },
        [&r, item, what] { r.check(r.clickItem(item), what); },
    };
}

std::filesystem::path fsPath(const QString& file)
{
    return std::filesystem::path(file.toStdWString());
}

std::size_t bodiesIn(const QString& file)
{
    auto loaded = io::loadProject(fsPath(file));
    return loaded ? loaded.value()->bodies().size() : 0;
}

double documentVolume(AcceptanceRunner& r)
{
    double total = 0;
    for (const auto& body : r.app().document().bodies())
        total += geom::volume(body->shape());
    return total;
}

// A project written as OpenShape writes it: boxes of these sizes (mm), side by side.
bool writeProject(const QString& file, const std::vector<Vec3>& sizes)
{
    doc::Document document;
    double x = 0;
    for (const Vec3& size : sizes) {
        auto body = std::make_unique<doc::Body>();
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = {x, 0, 0};
        box->size = size;
        x += size.x + 10;
        body->insertFeature(std::move(box), 0);
        document.addBody(std::move(body));
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    return io::saveProject(document, fsPath(file)).ok();
}

// ---- share ------------------------------------------------------------------------------

struct ShareState {
    std::unique_ptr<QTemporaryDir> dir;
    std::vector<ui::ShareRequest> requests;
    ui::ShareOutcome reply = ui::ShareOutcome::Completed; // what the stub sheet answers
    QString replyDetail;
    QStringList messages;
    QMetaObject::Connection listening;
    QString path(const QString& relative) const { return dir->path() + QLatin1Char('/') + relative; }
};

// The last share request was for `file` (relative to the app folder): the
// file is there and not empty, and the sheet points at the File button.
void checkShared(AcceptanceRunner& r, const std::shared_ptr<ShareState>& s, std::size_t count, const QString& file,
                 const QString& what)
{
    r.check(s->requests.size() == count, what + QStringLiteral(": the share sheet opens"),
            QString::number(s->requests.size()) + QStringLiteral(" requests"));
    if (s->requests.size() != count)
        return;
    const ui::ShareRequest& request = s->requests.back();
    const QFileInfo info(request.file);
    r.check(QFileInfo(request.file) == QFileInfo(s->path(file)), what + QStringLiteral(": it shares ") + file, request.file);
    r.check(info.isFile() && info.size() > 100, what + QStringLiteral(": the shared file is there, not empty"),
            QString::number(info.size()) + QStringLiteral(" bytes"));
    r.check(info.suffix() == QFileInfo(file).suffix(), what + QStringLiteral(": with the right extension"), info.suffix());
    QQuickItem* button = r.findItem(QStringLiteral("fileMenuButton"));
    const QRectF expected = button ? button->mapRectToScene(QRectF(0, 0, button->width(), button->height())) : QRectF();
    const QRectF& anchor = request.anchor;
    const bool near = !expected.isEmpty() && std::abs(anchor.x() - expected.x()) < 0.5 && std::abs(anchor.y() - expected.y()) < 0.5
                   && std::abs(anchor.width() - expected.width()) < 0.5 && std::abs(anchor.height() - expected.height()) < 0.5;
    r.check(near, what + QStringLiteral(": the iPad's popover points at the File button"),
            QStringLiteral("%1,%2 %3x%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height()));
}

Steps shareSteps(AcceptanceRunner& r)
{
    auto s = std::make_shared<ShareState>();
    auto& app = r.app();
    Steps steps;
    steps.push_back([&r, &app, s] {
        s->dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-share-XXXXXX"));
        r.check(s->dir->isValid(), "share: a temporary app folder");
        r.check(!app.canShare(), "share: no share sheet on the desktop");
        app.setAppFolder(s->dir->path());
        app.setTouchMode(true); // as on an iPhone or iPad
        app.setShareHandler([s](const ui::ShareRequest& request, ui::ShareFinished finished) {
            s->requests.push_back(request);
            finished(s->reply, s->replyDetail);
        });
        r.check(app.canShare(), "share: the (stub) share sheet is there");
        s->listening = QObject::connect(&app, &ui::AppController::message, &app, [s](const QString& t) { s->messages << t; });
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 3);
    // ---- Share Project… on a new project: its name first, then the sheet.
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "share: File menu"); });
    steps.push_back([&r, s] {
        r.check(shown(r, QStringLiteral("shareProjectMenuItem")), "share: File → Share Project… is there");
        r.screenshot(QStringLiteral("share_file_menu"));
        r.check(r.clickItem(QStringLiteral("shareProjectMenuItem")), "share: File → Share Project…");
        r.check(shown(r, QStringLiteral("saveNamePrompt")), "share: a new project asks for its name first");
        r.check(s->requests.empty(), "share: nothing shared before it is saved");
        r.type(QStringLiteral("Share lid"));
    });
    steps.push_back([&r, &app, s] {
        r.check(r.clickItem(QStringLiteral("saveNameSave")), "share: Save in the name prompt");
        checkShared(r, s, 1, QStringLiteral("Share lid.openshape"), QStringLiteral("share: Share Project"));
        r.check(!app.dirty() && app.documentTitle() == QStringLiteral("Share lid"), "share: the project is saved", app.documentTitle());
        r.check(bodiesIn(s->path(QStringLiteral("Share lid.openshape"))) == 1, "share: the shared file holds the box");
        // A second box: Share Project saves it first (no name asked).
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 3);
    append(steps, fileMenu(r, QStringLiteral("shareProjectMenuItem"), QStringLiteral("share: Share Project with unsaved changes")));
    steps.push_back([&r, &app, s] {
        r.check(!shown(r, QStringLiteral("saveNamePrompt")), "share: a saved project is not asked for a name");
        checkShared(r, s, 2, QStringLiteral("Share lid.openshape"), QStringLiteral("share: Share Project after changes"));
        r.check(!app.dirty() && bodiesIn(s->path(QStringLiteral("Share lid.openshape"))) == 2,
                "share: saved first: the shared file holds both boxes");
    });
    append(steps, fileMenu(r, QStringLiteral("shareProjectMenuItem"), QStringLiteral("share: Share Project, nothing changed")));
    steps.push_back([&r, s] { checkShared(r, s, 3, QStringLiteral("Share lid.openshape"), QStringLiteral("share: Share Project again")); });
    // ---- Exports: into Exports, then the sheet with the file.
    const std::pair<const char*, const char*> exports[] = {
        {"exportStlMenuItem", "Exports/Share lid.stl"},
        {"export3mfMenuItem", "Exports/Share lid.3mf"},
        {"exportStepMenuItem", "Exports/Share lid.step"},
    };
    std::size_t count = 3;
    for (const auto& [item, file] : exports) {
        const QString itemName = QString::fromLatin1(item);
        const QString fileName = QString::fromLatin1(file);
        append(steps, fileMenu(r, itemName, QStringLiteral("share: File → ") + itemName));
        ++count;
        steps.push_back([&r, s, fileName, count] {
            checkShared(r, s, count, fileName, QStringLiteral("share: ") + QFileInfo(fileName).suffix() + QStringLiteral(" export"));
            // The export's message stays (where the file is); the sheet's end says nothing.
            r.check(toastText(r) == QStringLiteral("Exported %1 to OpenShape → Exports (Files app)").arg(QFileInfo(fileName).fileName()),
                    "share: the export says where the file is", toastText(r));
        });
    }
    // ---- The sheet closed without sending, or failing.
    steps.push_back([s] {
        s->reply = ui::ShareOutcome::Cancelled;
        s->messages.clear();
    });
    append(steps, fileMenu(r, QStringLiteral("export3mfMenuItem"), QStringLiteral("share: Export 3MF, the sheet closed")));
    steps.push_back([&r, s] {
        r.check(s->requests.size() == 7, "share: the sheet opened", QString::number(s->requests.size()));
        r.check(s->messages == QStringList{QStringLiteral("Exported Share lid.3mf to OpenShape → Exports (Files app)")},
                "share: closing the sheet adds no message", s->messages.join(QStringLiteral(" | ")));
        s->reply = ui::ShareOutcome::Failed;
        s->replyDetail = QStringLiteral("The slicer could not take it");
    });
    append(steps, fileMenu(r, QStringLiteral("exportStlMenuItem"), QStringLiteral("share: Export STL, the sheet fails")));
    steps.push_back([&r, s] {
        r.check(toastText(r) == QStringLiteral("Could not share Share lid.stl: The slicer could not take it"),
                "share: a failed share is said", toastText(r));
        r.screenshot(QStringLiteral("share_failed"));
        s->reply = ui::ShareOutcome::Completed;
        s->replyDetail.clear();
        // An iPhone: the File button sits elsewhere; the sheet takes it.
        r.resizeWindow(402, 874);
        r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
    });
    wait(steps, 3);
    append(steps, fileMenu(r, QStringLiteral("exportStepMenuItem"), QStringLiteral("share: Export STEP on an iPhone")));
    steps.push_back([&r, s] {
        checkShared(r, s, 9, QStringLiteral("Exports/Share lid.step"), QStringLiteral("share: iPhone STEP export"));
        r.check(s->requests.back().anchor.top() >= 62, "share: the File button is below the Dynamic Island",
                QString::number(s->requests.back().anchor.top()));
    });
    append(steps, fileMenu(r, QStringLiteral("shareProjectMenuItem"), QStringLiteral("share: Share Project on an iPhone")));
    steps.push_back([&r, s] {
        checkShared(r, s, 10, QStringLiteral("Share lid.openshape"), QStringLiteral("share: iPhone Share Project"));
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
    });
    wait(steps, 2);
    // ---- The desktop is unchanged: no Share Project, exports ask where.
    steps.push_back([&r, &app] {
        app.setAppFolder({});
        app.setShareHandler(nullptr);
        r.check(!app.canShare(), "share: no share sheet again");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "share: File menu on the desktop");
    });
    steps.push_back([&r, s] {
        QQuickItem* item = r.findItem(QStringLiteral("shareProjectMenuItem"));
        r.check(item && !item->isVisible() && item->height() == 0, "share: no Share Project on the desktop (and no gap)");
        r.screenshot(QStringLiteral("share_desktop_menu"));
        r.key(Qt::Key_Escape);
        QObject::disconnect(s->listening);
        r.check(s->requests.size() == 10, "share: ten requests in all", QString::number(s->requests.size()));
    });
    return steps;
}

// ---- openin -----------------------------------------------------------------------------

struct OpenInState {
    std::unique_ptr<QTemporaryDir> dir;
    QString documents; // OpenShape's folder
    QString gearPlate; // a project elsewhere (iCloud Drive): 6000 + 1000 mm³
    QByteArray gearPlateBytes;
    QString mailedStep; // a STEP file Mail put into the Inbox: Mount 6000, Shaft 4000 mm³
    QString spacer;     // a STEP file elsewhere: 1200 mm³
    QString picked;     // a project elsewhere for the Open picker: 1000 mm³
    QString readme;     // not a model
    QStringList messages;
    QMetaObject::Connection listening;
};

QString lastMessage(const std::shared_ptr<OpenInState>& s)
{
    return s->messages.isEmpty() ? QString() : s->messages.last();
}

// What Qt's iOS scene delegate does with a file URL from openURLContexts.
void handOver(const QString& file)
{
    QWindowSystemInterface::handleFileOpenEvent(QUrl::fromLocalFile(file));
}

Steps openInSteps(AcceptanceRunner& r)
{
    auto s = std::make_shared<OpenInState>();
    auto& app = r.app();
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    Steps steps;
    steps.push_back([&r, &app, s] {
        s->dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-openin-XXXXXX"));
        r.check(s->dir->isValid(), "open in: a temporary sandbox");
        s->documents = s->dir->path() + QStringLiteral("/Documents");
        const QString elsewhere = s->dir->path() + QStringLiteral("/iCloud Drive");
        s->gearPlate = elsewhere + QStringLiteral("/Gear plate.openshape");
        s->picked = elsewhere + QStringLiteral("/Picked.openshape");
        s->mailedStep = s->documents + QStringLiteral("/Inbox/Motor mount.step");
        s->spacer = elsewhere + QStringLiteral("/Spacer.stp");
        s->readme = elsewhere + QStringLiteral("/readme.txt");
        r.check(writeProject(s->gearPlate, {{30, 20, 10}, {10, 10, 10}}), "open in: a project elsewhere (two bodies)");
        r.check(writeProject(s->picked, {{10, 10, 10}}), "open in: another project elsewhere");
        QFile gear(s->gearPlate);
        if (gear.open(QIODevice::ReadOnly))
            s->gearPlateBytes = gear.readAll();
        QDir().mkpath(QFileInfo(s->mailedStep).absolutePath());
        QDir().mkpath(QFileInfo(s->spacer).absolutePath());
        r.check(geom::exportStep({{"Mount", geom::makeBox({0, 0, 0}, {30, 20, 10}).value()},
                                  {"Shaft", geom::makeBox({40, 0, 0}, {10, 10, 40}).value()}},
                                 fsPath(s->mailedStep))
                    .ok(),
                "open in: a STEP file in the Inbox (as Mail leaves it)");
        r.check(geom::exportStep({{"Spacer", geom::makeBox({0, 0, 0}, {20, 20, 3}).value()}}, fsPath(s->spacer)).ok(),
                "open in: a STEP file elsewhere");
        QFile readme(s->readme);
        if (readme.open(QIODevice::WriteOnly))
            readme.write("not a model\n");
        app.setAppFolder(s->documents);
        app.setTouchMode(true);
        s->listening = QObject::connect(&app, &ui::AppController::message, &app, [s](const QString& t) { s->messages << t; });
        // Unsaved work, so the first file waits for "Save changes?".
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 3);
    // ---- A project from elsewhere, with unsaved changes: asked first.
    steps.push_back([&r, &app, s] {
        r.check(app.dirty() && app.bodyCount() == 1, "open in: unsaved changes");
        handOver(s->gearPlate);
    });
    steps.push_back([&r, &app, s] {
        r.check(shown(r, QStringLiteral("unsavedDialog")), "open in: Save changes? is asked first");
        r.check(app.bodyCount() == 1, "open in: nothing replaced yet");
        const QString copy = s->documents + QStringLiteral("/Gear plate.openshape");
        QFile file(copy);
        r.check(file.open(QIODevice::ReadOnly) && file.readAll() == s->gearPlateBytes,
                "open in: the project is already copied into OpenShape's folder, byte for byte");
        r.screenshot(QStringLiteral("openin_asked"));
        r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "open in: Don't Save");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s, num] {
        r.check(app.bodyCount() == 2 && std::abs(documentVolume(r) - 7000.0) < 1e-6, "open in: the project is open (7000 mm³)",
                QString::number(app.bodyCount()) + QStringLiteral(" bodies, ") + num(documentVolume(r)));
        r.check(app.documentTitle() == QStringLiteral("Gear plate") && !app.dirty(), "open in: named Gear plate, saved",
                app.documentTitle());
        r.check(app.projectFolder() == QUrl::fromLocalFile(s->documents).toString(),
                "open in: the open project is the copy in OpenShape's folder", app.projectFolder());
        r.check(lastMessage(s) == QStringLiteral("Opened “Gear plate”, a copy in OpenShape’s folder"),
                "open in: it says a copy was made", lastMessage(s));
        r.check(QFileInfo::exists(s->gearPlate), "open in: the original stays where it was");
        r.screenshot(QStringLiteral("openin_project"));
        handOver(s->gearPlate); // the same file again, nothing unsaved
    });
    wait(steps, 1);
    steps.push_back([&r, &app, s] {
        r.check(!shown(r, QStringLiteral("unsavedDialog")) && app.bodyCount() == 2, "open in: without changes it opens at once");
        r.check(QDir(s->documents).entryList({QStringLiteral("*.openshape")}, QDir::Files).size() == 1,
                "open in: the same file again makes no second copy");
        handOver(s->mailedStep); // Mail: "Open in OpenShape" on the attachment
    });
    wait(steps, 3);
    // ---- A STEP file from Mail's Inbox: a new project.
    steps.push_back([&r, &app, s, num] {
        r.check(app.bodyCount() == 2 && r.body(0).name() == "Mount" && r.body(1).name() == "Shaft",
                "open in: the STEP file's solids are the new project's bodies", QString::number(app.bodyCount()));
        r.check(std::abs(geom::volume(r.body(0).shape()) - 6000.0) < 1e-3 && std::abs(geom::volume(r.body(1).shape()) - 4000.0) < 1e-3,
                "open in: at the right size", num(geom::volume(r.body(0).shape())) + QStringLiteral(", ") + num(geom::volume(r.body(1).shape())));
        r.check(app.documentTitle() == QStringLiteral("Untitled") && !app.hasProjectPath(), "open in: a new, unsaved project",
                app.documentTitle());
        r.check(lastMessage(s) == QStringLiteral("Imported 2 bodies"), "open in: it says what it imported", lastMessage(s));
        r.check(!QFileInfo::exists(s->mailedStep) && !QFileInfo::exists(s->documents + QStringLiteral("/Inbox")),
                "open in: Mail's copy has left the Inbox (no Inbox left in OpenShape's folder)");
        r.check(!QFileInfo::exists(QDir::tempPath() + QStringLiteral("/openshape-incoming/Motor mount.step")),
                "open in: no scratch copy is left");
        r.check(!app.homeVisible(), "open in: Home is not in the way");
        // Unsaved (the import): the next file asks first; Cancel keeps all.
        handOver(s->spacer);
    });
    steps.push_back([&r, &app] {
        r.check(shown(r, QStringLiteral("unsavedDialog")), "open in: Save changes? for the STEP file");
        r.check(r.clickItem(QStringLiteral("unsavedCancel")), "open in: Cancel");
        r.check(app.bodyCount() == 2, "open in: Cancel keeps the project");
    });
    steps.push_back([s] { handOver(s->spacer); }); // handed over again
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("unsavedDialog")), "open in: asked again");
        r.check(r.clickItem(QStringLiteral("unsavedSave")), "open in: Save");
    });
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("saveNamePrompt")), "open in: a new project is asked for its name");
        r.type(QStringLiteral("Motor"));
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("saveNameSave")), "open in: Save the name"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s, num] {
        r.check(bodiesIn(s->documents + QStringLiteral("/Motor.openshape")) == 2, "open in: the imported project was saved as Motor");
        r.check(app.bodyCount() == 1 && std::abs(documentVolume(r) - 1200.0) < 1e-3, "open in: then the STEP file came in (1200 mm³)",
                QString::number(app.bodyCount()) + QStringLiteral(" bodies, ") + num(documentVolume(r)));
        r.check(QFileInfo::exists(s->spacer), "open in: a STEP file from elsewhere stays where it was");
        r.check(!QFileInfo::exists(QDir::tempPath() + QStringLiteral("/openshape-incoming/Spacer.stp")), "open in: its scratch copy is gone");
        // ---- A file OpenShape cannot use.
        s->messages.clear();
        handOver(s->readme);
    });
    wait(steps, 1);
    steps.push_back([&r, &app, s] {
        r.check(lastMessage(s) == QStringLiteral("OpenShape opens projects (.openshape) and STEP files (.step, .stp); "
                                                 "“readme.txt” is neither."),
                "open in: another kind of file is refused plainly", lastMessage(s));
        r.check(app.bodyCount() == 1 && !shown(r, QStringLiteral("unsavedDialog")), "open in: nothing changes");
        r.screenshot(QStringLiteral("openin_refused"));
        // ---- Home → Open…: the picker's project from elsewhere is copied in.
        app.setNextFileChoice(QUrl::fromLocalFile(s->picked));
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "open in: File menu");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeMenuItem")), "open in: File → Home"); });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeOpen")), "open in: Home → Open…"); });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "open in: Don't Save (the spacer)"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        r.check(app.documentTitle() == QStringLiteral("Picked") && app.bodyCount() == 1, "open in: the picked project is open",
                app.documentTitle());
        r.check(app.projectFolder() == QUrl::fromLocalFile(s->documents).toString() && QFileInfo::exists(s->documents + QStringLiteral("/Picked.openshape")),
                "open in: from a copy in OpenShape's folder (Save can write it)", app.projectFolder());
        // ---- File → Import STEP… from elsewhere reads a scratch copy.
        app.setNextFileChoice(QUrl::fromLocalFile(s->spacer));
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "open in: File menu");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("importStepMenuItem")), "open in: File → Import STEP…"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s, num] {
        r.check(app.bodyCount() == 2 && std::abs(documentVolume(r) - 2200.0) < 1e-3, "open in: the spacer is imported into the project",
                num(documentVolume(r)));
        r.check(!QFileInfo::exists(QDir::tempPath() + QStringLiteral("/openshape-incoming/Spacer.stp")), "open in: no scratch copy left");
        // ---- The desktop: files are opened where they are.
        app.setAppFolder({});
        app.newDocument();
        handOver(s->gearPlate);
    });
    wait(steps, 1);
    steps.push_back([&r, &app, s] {
        r.check(app.documentTitle() == QStringLiteral("Gear plate")
                    && app.projectFolder() == QUrl::fromLocalFile(QFileInfo(s->gearPlate).absolutePath()).toString(),
                "open in: on the desktop the file opens where it is", app.projectFolder());
        QObject::disconnect(s->listening);
        app.newDocument();
    });
    return steps;
}

const bool shareRegistered = registerAcceptanceScenario({QStringLiteral("share"), 96, shareSteps});
const bool openInRegistered = registerAcceptanceScenario({QStringLiteral("openin"), 97, openInSteps});

} // namespace
} // namespace os::app
