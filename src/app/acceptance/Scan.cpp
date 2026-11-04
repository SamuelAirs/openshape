// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Meshes as bodies: File -> Import STL... (two pieces become two bodies, one
// undo step, a damaged file says why), then a scan (AppController::
// addDemoScan: the Scan pipeline on synthetic photos, addMeshBody) becomes a
// selected body that push/pull works on. Native file dialogs cannot be
// clicked: the prepared file goes to AppController::setNextFileChoice.

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
    QString stl;
    QString garbage;
    QStringList messages;
    QMetaObject::Connection listening;
    double scanVolume = 0;
};

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    auto& app = r.app();
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    Steps steps;

    steps.push_back([&r, &app, s] {
        const QString dir =
            QFileInfo(app.recoverySession() ? app.recoverySession()->directory() : QDir::tempPath()).absolutePath();
        s->stl = dir + QStringLiteral("/scan_parts.stl");
        s->garbage = dir + QStringLiteral("/scan_garbage.stl");
        QFile::remove(s->stl);
        const Status written = geom::exportStl({{"a", geom::makeBox({0, 0, 0}, {30, 20, 10}).value()},
                                                {"b", geom::makeBox({50, 0, 0}, {10, 10, 10}).value()}},
                                               std::filesystem::path(s->stl.toStdWString()));
        r.check(written.ok(), "scan: an STL file with two boxes to import");
        QFile garbage(s->garbage);
        if (garbage.open(QIODevice::WriteOnly))
            garbage.write("this is not an STL file, only words that fill a few bytes");
        s->listening = QObject::connect(&app, &ui::AppController::message, &app, [s](const QString& t) { s->messages << t; });
        r.check(app.bodyCount() == 0, "scan: starts empty");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu");
    });
    steps.push_back([&r, &app, s] {
        app.setNextFileChoice(QUrl::fromLocalFile(s->stl));
        r.check(r.clickItem(QStringLiteral("importStlMenuItem")), "File → Import STL…");
    });
    steps.push_back([&r, &app, s, num] {
        r.check(app.bodyCount() == 2, "each closed piece becomes a body", QString::number(app.bodyCount()));
        if (app.bodyCount() == 2) {
            r.check(r.body(0).name() == "scan_parts 1" && r.body(1).name() == "scan_parts 2", "named after the file",
                    QString::fromStdString(r.body(0).name() + ", " + r.body(1).name()));
            const double a = geom::volume(r.body(0).shape()), b = geom::volume(r.body(1).shape());
            r.check(std::abs(a - 6000.0) < 1e-3 && std::abs(b - 1000.0) < 1e-3, "at their size (mm³)",
                    num(a) + QStringLiteral(", ") + num(b));
            r.check(r.body(0).shape().faceCount() == 6, "a box's coplanar triangles merge into 6 faces",
                    QString::number(r.body(0).shape().faceCount()));
        }
        r.check(!s->messages.isEmpty() && s->messages.last() == QStringLiteral("Imported 2 bodies"), "it says what it did",
                s->messages.isEmpty() ? QString() : s->messages.last());
        r.check(app.undoText() == QStringLiteral("Import 2 bodies"), "one undo step", app.undoText());
    });
    wait(steps, 3);
    steps.push_back([&r, &app] {
        r.screenshot(QStringLiteral("scan_01_stl"));
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.check(app.bodyCount() == 0, "undo removes the STL import", QString::number(app.bodyCount()));
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (damaged STL)");
    });
    steps.push_back([&r, &app, s] {
        s->messages.clear();
        app.setNextFileChoice(QUrl::fromLocalFile(s->garbage));
        r.check(r.clickItem(QStringLiteral("importStlMenuItem")), "Import STL… with a file that is not STL");
        r.check(app.bodyCount() == 0, "nothing is added", QString::number(app.bodyCount()));
        r.check(!s->messages.isEmpty() && s->messages.last().contains(QStringLiteral("STL")), "a plain message says why",
                s->messages.isEmpty() ? QString() : s->messages.last());
    });
    wait(steps, 2);

    // ---- A scan becomes a body ------------------------------------------------------
    steps.push_back([&r, &app, s, num] {
        QString summary;
        r.check(app.addDemoScan(&summary), "a scan of synthetic photos becomes a body", summary);
        r.check(app.bodyCount() == 1 && r.body(0).name() == "Scan 1", "named Scan 1",
                app.bodyCount() == 1 ? QString::fromStdString(r.body(0).name()) : QString::number(app.bodyCount()));
        const auto& sel = app.interaction().selection();
        r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Body, "and selected");
        r.check(app.undoText() == QStringLiteral("Import Scan 1"), "one undo step", app.undoText());
        if (app.bodyCount() == 1) {
            s->scanVolume = geom::volume(r.body(0).shape());
            const geom::BoundingBox box = geom::boundingBox(r.body(0).shape());
            // The block is 50 x 30 x 40 mm (an L, 33 750 mm³); the hull is a little larger.
            r.check(std::abs(box.size().x - 50) < 2.5 && std::abs(box.size().y - 30) < 2.5 && box.min.z > -1e-6,
                    "the scan has the block's size, standing on z = 0",
                    num(box.size().x) + QStringLiteral(" x ") + num(box.size().y) + QStringLiteral(" x ") + num(box.size().z));
            r.check(s->scanVolume > 0.97 * 33750 && s->scanVolume < 1.35 * 33750, "and about its volume", num(s->scanVolume));
        }
        r.check(!s->messages.isEmpty() && s->messages.last() == QStringLiteral("Added “Scan 1”"),
                "it says what it added", s->messages.isEmpty() ? QString() : s->messages.last());
    });
    wait(steps, 4); // the view frames the scan
    steps.push_back([&r, &app] {
        r.screenshot(QStringLiteral("scan_02_body"));
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.check(app.bodyCount() == 0, "undo removes the scan", QString::number(app.bodyCount()));
        r.key(Qt::Key_Y, Qt::ControlModifier);
        r.check(app.bodyCount() == 1, "redo brings it back", QString::number(app.bodyCount()));
    });
    steps.push_back([&r, s] {
        QObject::disconnect(s->listening);
        QFile::remove(s->stl);
        QFile::remove(s->garbage);
        r.key(Qt::Key_Escape);
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("scan"), 61, steps});

} // namespace
} // namespace os::app
