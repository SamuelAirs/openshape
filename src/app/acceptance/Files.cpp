// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Files: File -> Import STEP... (and Ctrl+I) with a STEP file written in
// inches, undo, push/pull on an imported body, a damaged file, save and
// reopen. Native file dialogs cannot be clicked: the scenario hands the
// prepared file to AppController::setNextFileChoice, and the menu item runs
// the dialog's onAccepted code with it.

#include "app/AcceptanceRunner.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"
#include "ui/RecoverySession.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
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

struct State {
    QString dir;
    QString step;    // "Bracket" 40 x 20 x 10 and "Pin" 10 x 10 x 30, written in inches
    QString garbage; // not a STEP file
    QString project;
    QStringList messages;
    QMetaObject::Connection listening;
};

std::filesystem::path toPath(const QString& file)
{
    return std::filesystem::path(file.toStdWString());
}

double volumeOf(AcceptanceRunner& r, std::size_t index)
{
    return geom::volume(r.body(index).shape());
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    auto& app = r.app();
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    Steps steps;

    // ---- Import STEP from the File menu ---------------------------------------------
    steps.push_back([&r, &app, s] {
        s->dir = QFileInfo(app.recoverySession() ? app.recoverySession()->directory() : QDir::tempPath()).absolutePath();
        s->step = s->dir + QStringLiteral("/files_parts.step");
        s->garbage = s->dir + QStringLiteral("/files_garbage.step");
        s->project = s->dir + QStringLiteral("/files_imported.openshape");
        QFile::remove(s->step);
        QFile::remove(s->project);
        const Status written = geom::exportStep({{"Bracket", geom::makeBox({0, 0, 0}, {40, 20, 10}).value()},
                                                 {"Pin", geom::makeBox({60, 0, 0}, {10, 10, 30}).value()}},
                                                toPath(s->step), {geom::StepUnit::Inch, false});
        r.check(written.ok(), "files: a STEP file in inches to import");
        QFile garbage(s->garbage);
        if (garbage.open(QIODevice::WriteOnly))
            garbage.write("ISO-10303-21;\nthis is not really a STEP file\n");
        s->listening = QObject::connect(&app, &ui::AppController::message, &app, [s](const QString& t) { s->messages << t; });
        r.check(app.bodyCount() == 0, "starts empty");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu");
    });
    steps.push_back([&r, &app, s, num] {
        app.setNextFileChoice(QUrl::fromLocalFile(s->step));
        r.check(r.clickItem(QStringLiteral("importStepMenuItem")), "File → Import STEP…");
        r.check(app.bodyCount() == 2, "each solid becomes a body", QString::number(app.bodyCount()));
        r.check(r.body(0).name() == "Bracket" && r.body(1).name() == "Pin", "named after the STEP products",
                QString::fromStdString(r.body(0).name() + ", " + r.body(1).name()));
        r.check(std::abs(volumeOf(r, 0) - 8000.0) < 1e-3 && std::abs(volumeOf(r, 1) - 3000.0) < 1e-3,
                "inches come in at the right size (mm³)", num(volumeOf(r, 0)) + QStringLiteral(", ") + num(volumeOf(r, 1)));
        r.check(!s->messages.isEmpty() && s->messages.last() == QStringLiteral("Imported 2 bodies"), "it says what it did",
                s->messages.isEmpty() ? QString() : s->messages.last());
        r.check(app.undoText() == QStringLiteral("Import 2 bodies"), "one undo step", app.undoText());
    });
    wait(steps, 4); // the view fits the new bodies
    steps.push_back([&r, &app] {
        r.screenshot(QStringLiteral("files_01_imported"));
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.check(app.bodyCount() == 0, "undo removes the import", QString::number(app.bodyCount()));
        r.key(Qt::Key_Y, Qt::ControlModifier);
        r.check(app.bodyCount() == 2, "redo brings it back", QString::number(app.bodyCount()));
    });
    wait(steps, 2);

    // ---- An imported body is like any other: push/pull its top ------------------------
    steps.push_back([&r, &app] {
        r.click(r.screenPoint(20, 10, 10));
        r.check(app.operationTitle() == QStringLiteral("Push/Pull"), "clicking the imported top face arms Push/Pull",
                app.operationTitle());
        r.type(QStringLiteral("25"));
        r.key(Qt::Key_Return);
    });
    steps.push_back([&r, num] {
        r.check(std::abs(volumeOf(r, 0) - 40.0 * 20.0 * 25.0) < 1e-3, "typing 25 makes it 25 mm high", num(volumeOf(r, 0)));
        r.check(r.body(0).features().size() == 2, "a Push/Pull step after the Import step");
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_Escape);
    });

    // ---- Ctrl+I, and a file that is not STEP ------------------------------------------
    steps.push_back([&r, &app, s] {
        app.setNextFileChoice(QUrl::fromLocalFile(s->step));
        r.key(Qt::Key_I, Qt::ControlModifier);
        r.check(app.bodyCount() == 4, "Ctrl+I imports too", QString::number(app.bodyCount()));
        r.check(app.bodyCount() == 4 && r.body(2).name() == "Bracket 2" && r.body(3).name() == "Pin 2",
                "names stay unique", app.bodyCount() == 4 ? QString::fromStdString(r.body(2).name()) : QString());
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (damaged file)");
    });
    steps.push_back([&r, &app, s] {
        s->messages.clear();
        app.setNextFileChoice(QUrl::fromLocalFile(s->garbage));
        r.check(r.clickItem(QStringLiteral("importStepMenuItem")), "Import STEP… with a damaged file");
        r.check(app.bodyCount() == 4, "nothing is added", QString::number(app.bodyCount()));
        r.check(!s->messages.isEmpty() && s->messages.last().contains(QStringLiteral("STEP")), "a plain message says why",
                s->messages.isEmpty() ? QString() : s->messages.last());
    });
    wait(steps, 3);

    // ---- Save and reopen: the imported geometry is in the project ---------------------
    steps.push_back([&r, &app, s, num] {
        const double pushed = volumeOf(r, 0);
        r.check(app.saveProjectAs(QUrl::fromLocalFile(s->project)), "save the project");
        app.newDocument();
        r.check(app.openProject(QUrl::fromLocalFile(s->project)), "reopen it");
        r.check(app.bodyCount() == 4, "four bodies again", QString::number(app.bodyCount()));
        r.check(std::abs(volumeOf(r, 0) - pushed) < 1e-6 && std::abs(volumeOf(r, 1) - 3000.0) < 1e-6,
                "with the same geometry", num(volumeOf(r, 0)));
        QObject::disconnect(s->listening);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.screenshot(QStringLiteral("files_02_reopened")); });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("files"), 60, steps});

} // namespace
} // namespace os::app
