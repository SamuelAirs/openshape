// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Files: File -> Import STEP... (and Ctrl+I) with a STEP file written in
// inches, undo, push/pull on an imported body, a damaged file, save and
// reopen with a thumbnail; Home (File -> Home): cards with previews, the
// card menu (button and long press), opening, New project, Open..., Import
// STEP..., Back and Esc; then, in a phone-sized window: Help closed over
// Home leaves the keys with Home, a damaged file chosen on Home shows its
// message above Home, and a long message (what an import skipped) wraps;
// names from the file are shown as plain text. Native file dialogs cannot
// be clicked: the scenario hands the prepared file to
// AppController::setNextFileChoice, and the button or menu item runs the
// dialog's onAccepted code with it.

#include "app/AcceptanceRunner.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"
#include "io/ProjectFile.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"
#include "ui/RecoverySession.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

const QString kNameB = QStringLiteral("files_b José Ж");

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

struct State {
    QString dir;
    QString step;    // "Bracket" 40 x 20 x 10 and "Pin" 10 x 10 x 30, written in inches
    QString garbage; // not a STEP file
    QString mixed;   // a solid named "<b>Block</b>" and an open disc
    QSize windowSize;
    QSize minimumSize;
    QString project;
    QString projectB;
    QPointF pressAt;
    QStringList messages;
    QMetaObject::Connection listening;
};

std::filesystem::path toPath(const QString& file)
{
    return std::filesystem::path(file.toStdWString());
}

// Every Text item in the visual tree (declared ones and generated delegates).
void collectTexts(QQuickItem* item, QList<QQuickItem*>& out)
{
    if (item->inherits("QQuickText"))
        out << item;
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children)
        collectTexts(child, out);
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
        // The project carries a preview of the model, readable on its own.
        auto png = io::readProjectThumbnail(toPath(s->project));
        r.check(png.ok(), "the saved project has a thumbnail", QString::fromStdString(png.developerMessage()));
        const QImage thumbnail = png ? QImage::fromData(png.value().data(), int(png.value().size()), "PNG") : QImage();
        r.check(thumbnail.width() == ui::kThumbnailSize && thumbnail.height() == ui::kThumbnailSize,
                "a 256 x 256 picture", QStringLiteral("%1 x %2").arg(thumbnail.width()).arg(thumbnail.height()));
        if (!thumbnail.isNull()) {
            thumbnail.save(r.outputDir() + QStringLiteral("/files_03_thumbnail.png"));
            int opaque = 0;
            for (int y = 0; y < thumbnail.height(); y += 4)
                for (int x = 0; x < thumbnail.width(); x += 4)
                    opaque += qAlpha(thumbnail.pixel(x, y)) == 255 ? 1 : 0;
            r.check(qAlpha(thumbnail.pixel(1, 1)) == 0 && opaque > 200, "the model on a clear background",
                    QString::number(opaque) + QStringLiteral(" opaque samples"));
        }
        app.newDocument();
        r.check(app.openProject(QUrl::fromLocalFile(s->project)), "reopen it");
        r.check(app.bodyCount() == 4, "four bodies again", QString::number(app.bodyCount()));
        r.check(std::abs(volumeOf(r, 0) - pushed) < 1e-6 && std::abs(volumeOf(r, 1) - 3000.0) < 1e-6,
                "with the same geometry", num(volumeOf(r, 0)));
        QObject::disconnect(s->listening);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.screenshot(QStringLiteral("files_02_reopened")); });

    // ---- Home: recent projects with previews ------------------------------------------
    steps.push_back([&r, &app, s] {
        // A name outside ASCII: its preview must still load (Qt hands the
        // image provider its source partly decoded).
        s->projectB = s->dir + QLatin1Char('/') + kNameB + QStringLiteral(".openshape");
        QFile::remove(s->projectB);
        // Only this scenario's projects in the list (others ran before it).
        app.clearRecentFiles();
        r.check(app.openProject(QUrl::fromLocalFile(s->project)), "open the imported project again (recent: A)");
        app.newDocument();
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.saveProjectAs(QUrl::fromLocalFile(s->projectB)), "save a second project (B)");
        r.check(!app.homeVisible(), "no Home in automated runs until asked");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (Home)");
    });
    steps.push_back([&r, &app] {
        r.check(r.clickItem(QStringLiteral("homeMenuItem")), "File → Home");
        r.check(app.homeVisible(), "Home is shown");
    });
    wait(steps, 4); // previews load in the background
    steps.push_back([&r, &app] {
        const QVariantList projects = app.homeProjects();
        r.check(projects.size() == 2 && projects[0].toMap()[QStringLiteral("name")] == kNameB,
                "Home lists the recent projects, newest first", QString::number(projects.size()));
        QQuickItem* first = r.findItem(QStringLiteral("homeCard_0"));
        QQuickItem* second = r.findItem(QStringLiteral("homeCard_1"));
        r.check(first && second && first->isVisible() && second->isVisible(), "one card each");
        QQuickItem* preview = r.findItem(QStringLiteral("homeThumbnail_0"));
        r.check(preview && preview->property("status").toInt() == 1 /* Image.Ready */
                    && preview->property("sourceSize").toSize().width() > 0,
                "the card shows the project's preview (a name outside ASCII)");
        QQuickItem* previewA = r.findItem(QStringLiteral("homeThumbnail_1"));
        r.check(previewA && previewA->property("status").toInt() == 1, "and the other card its own");
        r.check(first && first->width() >= 150, "cards are touch-sized", first ? QString::number(first->width()) : QString());
        r.screenshot(QStringLiteral("files_04_home"));
        r.check(r.clickItem(QStringLiteral("homeCardMenu_1")), "a card's ⋯ menu");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("homeRemove_1")), "Remove from list");
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        r.check(app.homeProjects().size() == 1 && app.recentFiles().size() == 1, "the project leaves the list",
                QString::number(app.homeProjects().size()));
        r.check(QFileInfo::exists(s->project), "but the file stays");
        // A long press (touch) opens the same menu.
        QQuickItem* card = r.findItem(QStringLiteral("homeCard_0"));
        if (card) {
            s->pressAt = card->mapToScene(QPointF(card->width() / 2, card->height() / 3));
            r.mousePress(s->pressAt);
        }
    });
    wait(steps, 4); // 640 ms > the 500 ms press-and-hold
    steps.push_back([&r, s] {
        r.mouseRelease(s->pressAt);
        QQuickItem* remove = r.findItem(QStringLiteral("homeRemove_0"));
        r.check(remove && remove->isVisible(), "a long press opens the card's menu");
        r.key(Qt::Key_Escape);
    });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.check(app.homeVisible(), "Esc closes only the menu");
        r.check(r.clickItem(QStringLiteral("homeCard_0")), "tap a project card");
        r.check(!app.homeVisible() && app.documentTitle() == kNameB, "it opens, and Home closes",
                app.documentTitle());
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (Home again)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeMenuItem")), "File → Home (New project)"); });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.check(r.clickItem(QStringLiteral("homeNew")), "New project button");
        r.check(!app.homeVisible() && app.bodyCount() == 0 && app.documentTitle() == QStringLiteral("Untitled"),
                "an empty new document");
        QQuickItem* empty = r.findItem(QStringLiteral("emptyState"));
        r.check(empty && empty->isVisible(), "with the \"Start with a shape\" card");
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (Open from Home)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeMenuItem")), "File → Home (Open)"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        app.setNextFileChoice(QUrl::fromLocalFile(s->project));
        r.check(r.clickItem(QStringLiteral("homeOpen")), "Open… button");
        r.check(!app.homeVisible() && app.documentTitle() == QStringLiteral("files_imported") && app.bodyCount() == 4,
                "it opens the chosen project", app.documentTitle());
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (Import from Home)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeMenuItem")), "File → Home (Import)"); });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        app.setNextFileChoice(QUrl::fromLocalFile(s->step));
        r.check(r.clickItem(QStringLiteral("homeImport")), "Import STEP… button");
        r.check(!app.homeVisible() && app.documentTitle() == QStringLiteral("Untitled") && app.bodyCount() == 2,
                "a new document with the imported bodies", QString::number(app.bodyCount()));
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "File menu (Back)");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeMenuItem")), "File → Home (Back)"); });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.check(r.clickItem(QStringLiteral("homeBack")), "Back to the model");
        r.check(!app.homeVisible() && app.bodyCount() == 2, "the document is still there");
        app.setHomeVisible(true);
    });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.key(Qt::Key_Escape);
        r.check(!app.homeVisible(), "Esc leaves Home too");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(app.bodyCount() == 3 && app.dirty(), "B adds a box: unsaved changes");
        app.setHomeVisible(true);
    });
    wait(steps, 2);
    steps.push_back([&r, &app, s] {
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.check(app.bodyCount() == 3, "Ctrl+Z does nothing behind Home", QString::number(app.bodyCount()));
        // Ctrl+I on Home starts a new project, asking about the unsaved box first.
        app.setNextFileChoice(QUrl::fromLocalFile(s->step));
        r.key(Qt::Key_I, Qt::ControlModifier);
        QQuickItem* question = r.findItem(QStringLiteral("unsavedDialog"));
        r.check(question && question->isVisible(), "Ctrl+I on Home asks about unsaved changes");
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "Don't Save"); });
    wait(steps, 2);
    steps.push_back([&r, &app] {
        r.check(!app.homeVisible() && app.bodyCount() == 2 && !app.canRedo() && app.undoText() == QStringLiteral("Import 2 bodies"),
                "a new project with the imported bodies", QString::number(app.bodyCount()));
    });

    // ---- On a phone: Help over Home, messages over Home, long messages -----------------
    steps.push_back([&r, &app, s] {
        // A solid named with markup and an open surface (which is skipped).
        s->mixed = s->dir + QStringLiteral("/files_mixed.step");
        QFile::remove(s->mixed);
        geom::PlaneFrame plane;
        plane.origin = {0, 0, 40};
        auto disc = geom::findRegions(plane, {{geom::PlanarCurve::Kind::Circle, {}, {}, {0, 0, 40}, 8.0}});
        r.check(disc.ok() && disc.value().size() == 1, "an open disc to skip");
        if (disc.ok() && disc.value().size() == 1) {
            const Status written = geom::exportStep({{"<b>Block</b>", geom::makeBox({0, 0, 0}, {30, 20, 10}).value()},
                                                     {"Disc", disc.value()[0].face}},
                                                    toPath(s->mixed));
            r.check(written.ok(), "a STEP file with a solid and an open surface");
        }
        app.newDocument(); // nothing unsaved to ask about
        app.setHomeVisible(true);
        s->windowSize = r.window()->size();
        s->minimumSize = r.window()->minimumSize();
        r.window()->setMinimumSize({});
        r.window()->resize(402, 874);
    });
    wait(steps, 3);
    steps.push_back([&r, &app] {
        r.check(r.window()->width() == 402, "a phone-sized window", QString::number(r.window()->width()));
        r.screenshot(QStringLiteral("files_05_phone_home"));
        r.key(Qt::Key_F1);
        QQuickItem* help = r.findItem(QStringLiteral("helpOverlay"));
        r.check(help && help->isVisible(), "F1 opens Help over Home");
        QQuickItem* close = r.findItem(QStringLiteral("helpClose"));
        const QPointF right = close ? close->mapToScene(QPointF(close->width(), 0)) : QPointF();
        r.check(close && right.x() <= r.window()->width() - 24, "Help's Close button is on a phone's screen",
                QString::number(right.x()));
        r.screenshot(QStringLiteral("files_05b_phone_help"));
        r.check(r.clickItem(QStringLiteral("helpClose")), "Help's Close button");
        r.check(help && !help->isVisible(), "it closes Help");
        QQuickItem* home = r.findItem(QStringLiteral("homeScreen"));
        r.check(app.homeVisible() && home && home->hasActiveFocus(), "closing Help gives the keys back to Home");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
        r.check(app.bodyCount() == 0 && !app.sketchMode() && !app.dirty(), "B and K do nothing to the model behind Home");
    });
    steps.push_back([&r, &app, s] {
        s->messages.clear();
        s->listening = QObject::connect(&app, &ui::AppController::message, &app, [s](const QString& t) { s->messages << t; });
        app.setNextFileChoice(QUrl::fromLocalFile(s->garbage));
        r.check(r.clickItem(QStringLiteral("homeImport")), "Import STEP… on Home with a damaged file");
        r.check(app.homeVisible() && app.bodyCount() == 0, "Home stays");
        r.check(!s->messages.isEmpty() && s->messages.last().contains(QStringLiteral("STEP")), "a message says why",
                s->messages.isEmpty() ? QString() : s->messages.last());
    });
    wait(steps, 2); // the message fades in
    steps.push_back([&r, s] {
        QQuickItem* toast = r.findItem(QStringLiteral("toast"));
        QQuickItem* text = r.findItem(QStringLiteral("toastText"));
        QQuickItem* home = r.findItem(QStringLiteral("homeScreen"));
        const QString shown = text ? text->property("text").toString() : QString();
        r.check(!s->messages.isEmpty() && shown == s->messages.last(), "the message shows why", shown);
        r.check(toast && home && toast->isVisible() && toast->z() > home->z(), "the message is above Home",
                toast && home ? QStringLiteral("z %1 over %2").arg(toast->z()).arg(home->z()) : QString());
        // And it is what the window shows there (the dark message, not Home).
        if (toast) {
            const QImage shot = r.window()->grabWindow();
            const QPointF at = toast->mapToScene(QPointF(12, toast->height() / 2)) * shot.devicePixelRatio();
            const QRgb pixel = shot.pixel(at.toPoint());
            r.check(qGray(pixel) < 90, "the message is drawn over Home", QString::number(qGray(pixel)));
        }
        r.screenshot(QStringLiteral("files_06_phone_home_message"));
    });
    steps.push_back([&r, &app, s] {
        s->messages.clear();
        app.setNextFileChoice(QUrl::fromLocalFile(s->mixed));
        r.check(r.clickItem(QStringLiteral("homeImport")), "Import STEP… on Home with a solid and a surface");
        r.check(!app.homeVisible() && app.bodyCount() == 1, "the solid becomes a new project's body",
                QString::number(app.bodyCount()));
        r.check(app.bodyCount() == 1 && r.body(0).name() == "<b>Block</b>", "named as in the file",
                app.bodyCount() == 1 ? QString::fromStdString(r.body(0).name()) : QString());
        r.check(!s->messages.isEmpty() && s->messages.last().contains(QStringLiteral("Skipped 1 open surface")),
                "the message says what was skipped", s->messages.isEmpty() ? QString() : s->messages.last());
        QObject::disconnect(s->listening);
    });
    wait(steps, 3);
    steps.push_back([&r, s] {
        QQuickItem* toast = r.findItem(QStringLiteral("toast"));
        QQuickItem* text = r.findItem(QStringLiteral("toastText"));
        const QString shown = text ? text->property("text").toString() : QString();
        r.check(toast && text && toast->isVisible() && !s->messages.isEmpty() && shown == s->messages.last(),
                "the long message is shown", shown);
        if (toast && text) {
            const QPointF left = toast->mapToScene(QPointF(0, 0));
            r.check(left.x() >= 0 && left.x() + toast->width() <= r.window()->width(), "it fits the phone's width",
                    QStringLiteral("x %1, width %2").arg(left.x()).arg(toast->width()));
            r.check(text->property("lineCount").toInt() >= 2 && text->height() + 16 <= toast->height(),
                    "wrapped onto more lines inside the message", QString::number(text->property("lineCount").toInt()));
        }
        // Names from the file are shown as they are, never as HTML (Text's
        // automatic format would render the markup, and could load images).
        int showing = 0;
        bool plain = true;
        QList<QQuickItem*> texts;
        collectTexts(r.window()->contentItem(), texts);
        for (QQuickItem* item : texts)
            if (item->property("text").toString().contains(QStringLiteral("<b>Block</b>"))) {
                ++showing;
                plain = plain && item->property("textFormat").toInt() == 0; // Text.PlainText
            }
        r.check(showing >= 2 && plain, "the markup in the name is shown as text (Model panel, message)",
                QString::number(showing) + QStringLiteral(" items"));
        r.screenshot(QStringLiteral("files_07_phone_long_message"));
        // Back to the usual window for the scenarios after this one.
        r.window()->resize(s->windowSize);
        r.window()->setMinimumSize(s->minimumSize);
    });
    wait(steps, 3);
    steps.push_back([&r, s] {
        r.check(r.window()->size() == s->windowSize, "the window has its size back");
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("files"), 60, steps});

} // namespace
} // namespace os::app
