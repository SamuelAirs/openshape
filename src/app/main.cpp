// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "app/AcceptanceRunner.h"
#include "app/CrashLog.h"
#include "core/Log.h"
#include "core/Version.h"
#include "geometry/Modeling.h"
#include "ui/AppController.h"
#include "ui/AppSettings.h"
#include "ui/ThumbnailProvider.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLockFile>
#include <QtCore/QMap>
#include <QtCore/QScopeGuard>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtGui/QStyleHints>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>

#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>

Q_IMPORT_QML_PLUGIN(OpenShapePlugin)

namespace {

// The log folder: in the user's app-data folder (on iPadOS in Documents,
// which the Files app shows, so testers can send it), or <data-dir>/logs.
QString logDirectory(const QString& dataDir)
{
    if (!dataDir.isEmpty())
        return dataDir + QStringLiteral("/logs");
#if defined(Q_OS_IOS)
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Logs");
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
#endif
}

// Log sink: stderr plus a rolling file (openshape.log in `dir`).
void installLogging(const QString& dir)
{
    QDir().mkpath(dir);
    static QFile file(dir + QStringLiteral("/openshape.log"));
    if (file.size() > 4 * 1024 * 1024)
        file.remove();
    if (!file.open(QIODevice::Append | QIODevice::Text))
        std::fprintf(stderr, "warning: cannot open log file %s\n", qPrintable(file.fileName()));

    os::setLogSink([](os::LogLevel level, os::LogCategory category, const std::string& message) {
        const std::string line = "[" + std::string(os::toString(level)) + "] " + std::string(os::toString(category)) + ": "
                               + message + "\n";
        std::fputs(line.c_str(), stderr);
        if (file.isOpen()) {
            file.write(line.data(), qint64(line.size()));
            file.flush();
        }
    });

    // Route Qt's own diagnostics (QML errors, RHI warnings) into the same log.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& text) {
        const os::LogLevel level = type == QtDebugMsg || type == QtInfoMsg ? os::LogLevel::Info
            : type == QtWarningMsg                                         ? os::LogLevel::Warning
                                                                           : os::LogLevel::Error;
        os::log(level, os::LogCategory::App, "Qt: " + text.toStdString());
    });
}

// The README's hero shot: a small project box built as a user would (a box,
// three sizes typed, rounded corners, shelled, a cable hole in one end),
// left with the hole's new diameter being typed (the print-tolerance step).
void runEnclosureDemo(os::ui::AppController& app)
{
    auto& interaction = app.interaction();
    auto clickAt = [&](const os::Vec3& world, bool add = false) {
        os::interact::PointerEvent e;
        e.position = interaction.camera().project(world);
        e.modifiers.shift = add;
        interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, e.position, {}});
        interaction.pointerPress(e);
        interaction.pointerRelease(e);
    };
    auto apply = [&](const char* value) {
        interaction.setValueText(value);
        (void)interaction.commitOperation();
        interaction.setStandardView(os::StandardView::Isometric, false);
        interaction.fitAll(false);
    };
    app.createBox(20); // x, y in [-10, 10], z in [0, 20]
    interaction.fitAll(false);
    clickAt({10, 0, 10});
    apply("60"); // width: x in [-10, 50]
    clickAt({20, -10, 10});
    apply("40"); // depth: y in [-30, 10]
    clickAt({20, -10, 20});
    apply("25"); // height: z in [0, 25]
    // Round the four upright edges; the one at the back is picked from behind.
    clickAt({50, -30, 12});
    clickAt({-10, -30, 12}, true);
    clickAt({50, 10, 12}, true);
    interaction.twoFingerRotate(os::kPi / 0.008, 0); // half a turn (Camera orbits 0.008 rad per pixel)
    clickAt({-10, 10, 12}, true);
    apply("6");
    clickAt({20, -10, 25});
    (void)interaction.triggerAction("shell");
    apply("2");
    // A cable hole: a 10 mm circle on the right end, cut through the wall.
    clickAt({50, -10, 20});
    (void)interaction.startSketch();
    interaction.skipAnimation();
    interaction.setSketchTool(os::interact::SketchTool::Circle);
    clickAt({50, -10, 12.5});
    interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None,
                             interaction.camera().project({50, -6, 12.5}), {}});
    interaction.sketchSession()->typeIntoInput("10");
    interaction.keyPress(os::interact::Key::Enter);
    interaction.finishSketch();
    interaction.setStandardView(os::StandardView::Isometric, false);
    interaction.fitAll(false);
    clickAt({50, -10, 12.5});
    apply("-4");
    // The hole's wall: its diameter, typed with a little print clearance.
    // Zoomed in to click it, as a user would: the 2 mm wall must be wider
    // than the edge pick tolerance (6 px), or the click picks a rim.
    const os::Vec3 wall{49, -10, 7.5};
    const os::Vec2 hole = interaction.camera().project(wall);
    int steps = 0;
    for (; steps < 30 && interaction.camera().pixelSize(wall) > 1.0 / 15; ++steps)
        interaction.wheel(hole, 1);
    clickAt(wall);
    interaction.wheel(hole, -steps);
    interaction.wheel(interaction.camera().project({20, -10, 12.5}), 2);
    interaction.setValueText("10.4");
}

// Scripted demo used for screenshots and smoke tests: builds the Milestone 0
// state (cube, top face selected, push/pull preview to a 35 mm height).
// `dataDir` is the run's scratch folder (where "home" saves its projects).
void runDemo(os::ui::AppController& app, const QString& demo, const QString& dataDir = {})
{
    auto& interaction = app.interaction();
    interaction.fitAll(false);
    if (demo == QLatin1String("empty"))
        return;
    if (demo == QLatin1String("home")) {
        // Home with a few saved projects (and their previews).
        const QString dir = (dataDir.isEmpty() ? QDir::tempPath() : dataDir) + QStringLiteral("/projects");
        QDir().mkpath(dir);
        auto save = [&](const QString& name) {
            (void)app.saveProjectAs(QUrl::fromLocalFile(dir + QLatin1Char('/') + name + QStringLiteral(".openshape")));
            app.newDocument();
        };
        runDemo(app, QStringLiteral("committed"));
        save(QStringLiteral("Tall block"));
        for (int i = 0; i < 3; ++i)
            app.createBox(20);
        save(QStringLiteral("Three blocks"));
        runDemo(app, QStringLiteral("fillet"));
        app.commitOperation();
        save(QStringLiteral("Rounded block"));
        runDemo(app, QStringLiteral("bracket"));
        save(QStringLiteral("Mounting bracket"));
        app.setHomeVisible(true);
        return;
    }
    if (demo == QLatin1String("enclosure")) {
        runEnclosureDemo(app);
        return;
    }
    if (demo == QLatin1String("revolve")) {
        using P = os::interact::InteractionController::SketchPlane;
        (void)interaction.startSketch(P::Front);
        interaction.skipAnimation();
        auto screenOf = [&](os::Vec2 local) {
            return interaction.camera().project(interaction.sketchSession()->sketch().plane().toWorld(local));
        };
        os::interact::PointerEvent e;
        e.position = screenOf({6, 0});
        interaction.pointerPress(e);
        e.position = screenOf({10, 14});
        interaction.pointerMove(e);
        interaction.pointerRelease(e);
        interaction.finishSketch();
        interaction.setStandardView(os::StandardView::Isometric, false);
        interaction.fitAll(false);
        os::interact::PointerEvent pick;
        pick.position = interaction.camera().project({8, 0, 7});
        interaction.pointerPress(pick);
        interaction.pointerRelease(pick);
        (void)interaction.triggerAction("revolve");
        interaction.setValueText("270");
        return;
    }
    if (demo.startsWith(QLatin1String("sketch")) || demo == QLatin1String("extrude") || demo == QLatin1String("bracket")) {
        using os::interact::Key;
        using os::interact::SketchTool;
        auto screenOf = [&](os::Vec2 local) {
            return interaction.camera().project(interaction.sketchSession()->sketch().plane().toWorld(local));
        };
        auto clickAt = [&](os::Vec2 screen) {
            os::interact::PointerEvent e;
            e.position = screen;
            interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, screen, {}});
            interaction.pointerPress(e);
            interaction.pointerRelease(e);
        };
        (void)interaction.startSketch();
        interaction.skipAnimation();
        clickAt(screenOf({0, 0}));
        interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, screenOf({38, 24}), {}});
        interaction.sketchSession()->typeIntoInput("60");
        if (demo == QLatin1String("sketch"))
            return; // mid-rectangle: width typed, height following the cursor
        interaction.sketchSession()->focusNextInput();
        interaction.sketchSession()->typeIntoInput(demo == QLatin1String("bracket") ? "30" : "40");
        interaction.keyPress(Key::Enter);
        if (demo == QLatin1String("sketchdone")) {
            interaction.setSketchTool(SketchTool::Circle);
            clickAt(screenOf({15, 20}));
            interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, screenOf({18, 20}), {}});
            interaction.sketchSession()->typeIntoInput("8");
            interaction.keyPress(Key::Enter);
            return;
        }
        interaction.finishSketch();
        interaction.fitAll(false);
        clickAt(interaction.camera().project({30, 15, 0}));
        interaction.setValueText(demo == QLatin1String("bracket") ? "5" : "20");
        if (demo == QLatin1String("extrude"))
            return; // preview with the value chip
        (void)interaction.commitOperation();
        // Hole sketch on the top face: two 6 mm circles, cut through.
        clickAt(interaction.camera().project({30, 15, 5}));
        (void)interaction.startSketch();
        interaction.skipAnimation();
        for (double x : {10.0, 50.0}) {
            interaction.setSketchTool(SketchTool::Circle);
            clickAt(screenOf({x, 15}));
            interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, screenOf({x + 3, 15}), {}});
            interaction.sketchSession()->typeIntoInput("6");
            interaction.keyPress(Key::Enter);
        }
        interaction.finishSketch();
        interaction.setStandardView(os::StandardView::Isometric, false);
        interaction.fitAll(false);
        clickAt(interaction.camera().project({10, 15, 5}));
        os::interact::PointerEvent shiftClick;
        shiftClick.position = interaction.camera().project({50, 15, 5});
        shiftClick.modifiers.shift = true;
        interaction.pointerPress(shiftClick);
        interaction.pointerRelease(shiftClick);
        interaction.setValueText("-5");
        (void)interaction.commitOperation();
        return;
    }
    if (demo == QLatin1String("arc")) {
        // A "D" (line + arc) and a second arc being bent.
        using os::interact::SketchTool;
        (void)interaction.startSketch();
        interaction.skipAnimation();
        auto at = [&](os::Vec2 local) {
            return interaction.camera().project(interaction.sketchSession()->sketch().plane().toWorld(local));
        };
        auto clickAt = [&](os::Vec2 local) {
            os::interact::PointerEvent e;
            e.position = at(local);
            interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, e.position, {}});
            interaction.pointerPress(e);
            interaction.pointerRelease(e);
        };
        interaction.setSketchTool(SketchTool::Line);
        clickAt({0, 0});
        clickAt({20, 0});
        interaction.keyPress(os::interact::Key::Escape);
        interaction.setSketchTool(SketchTool::Arc);
        clickAt({20, 0});
        clickAt({0, 0});
        clickAt({10, 6});
        clickAt({30, 0});
        clickAt({50, 0});
        interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, at({40, -8}), {}});
        return;
    }
    if (demo == QLatin1String("polygon") || demo == QLatin1String("constraints")) {
        // A center rectangle, and a hexagon being drawn (size typed, sides
        // shown); "constraints" completes it and shows the constraint glyphs.
        using os::interact::SketchTool;
        (void)interaction.startSketch();
        interaction.skipAnimation();
        auto at = [&](os::Vec2 local) {
            return interaction.camera().project(interaction.sketchSession()->sketch().plane().toWorld(local));
        };
        auto moveTo = [&](os::Vec2 local) {
            interaction.pointerMove({os::interact::PointerDevice::Mouse, os::interact::PointerButton::None, at(local), {}});
        };
        auto clickAt = [&](os::Vec2 local) {
            os::interact::PointerEvent e;
            e.position = at(local);
            moveTo(local);
            interaction.pointerPress(e);
            interaction.pointerRelease(e);
        };
        interaction.setSketchTool(SketchTool::CenterRectangle);
        clickAt({0, 0});
        moveTo({20, 10});
        interaction.sketchSession()->typeIntoInput("40");
        interaction.sketchSession()->focusNextInput();
        interaction.sketchSession()->typeIntoInput("20");
        interaction.keyPress(os::interact::Key::Enter);
        interaction.setSketchTool(SketchTool::Polygon);
        clickAt({45, 0});
        moveTo({55, 0});
        interaction.sketchSession()->typeIntoInput("16");
        if (demo == QLatin1String("polygon"))
            return;
        interaction.keyPress(os::interact::Key::Enter);
        interaction.setSketchTool(SketchTool::Line);
        clickAt({-20, 25});
        clickAt({10, 25});
        interaction.keyPress(os::interact::Key::Escape);
        interaction.setSketchTool(SketchTool::TangentArc);
        clickAt({10, 25});
        clickAt({10, 45});
        interaction.keyPress(os::interact::Key::Escape);
        interaction.setSketchTool(SketchTool::Select);
        for (int i = 0; i < 3; ++i)
            interaction.wheel(at({18, 12}), 1.0);
        return;
    }
    if (demo == QLatin1String("mirror") || demo == QLatin1String("pattern")) {
        // Mirror across the cube's +X face, or the default linear pattern.
        app.createBox(20);
        interaction.fitAll(false);
        (void)interaction.selectBody(app.document().bodies().front()->id(), false);
        (void)interaction.triggerAction(demo.toStdString());
        if (demo == QLatin1String("mirror")) {
            os::interact::PointerEvent face;
            face.position = interaction.camera().project({10, 0, 10});
            interaction.pointerPress(face);
            interaction.pointerRelease(face);
        }
        interaction.fitAll(false);
        return;
    }
    if (demo == QLatin1String("holes")) {
        // The Hole tool on the cube's top: two countersunk M3 holes, the
        // second one's Y being typed.
        app.createBox(20);
        interaction.fitAll(false);
        const os::Vec3 taps[] = {{3, -3, 20}, {0, 0, 20}, {-5, 5, 20}}; // the face, then two holes
        for (std::size_t i = 0; i < std::size(taps); ++i) {
            os::interact::PointerEvent tap;
            tap.position = interaction.camera().project(taps[i]);
            interaction.pointerPress(tap);
            interaction.pointerRelease(tap);
            if (i == 0)
                (void)interaction.triggerAction("hole");
        }
        (void)interaction.triggerAction("head:countersink");
        (void)interaction.triggerAction("field:y");
        interaction.setValueText("15");
        return;
    }
    if (demo == QLatin1String("rotate")) {
        // A body in Rotate mode with a 30 degree preview about Z.
        app.createBox(20);
        interaction.fitAll(false);
        (void)interaction.selectBody(app.document().bodies().front()->id(), false);
        (void)interaction.triggerAction("rotate");
        interaction.setValueText("30");
        return;
    }
    if (demo == QLatin1String("combine") || demo == QLatin1String("history")) {
        // Two bodies selected from the model panel (combine actions), or a
        // fillet step highlighted in the view (history).
        app.createBox(20);
        app.createBox(20);
        interaction.fitAll(false);
        const auto& bodies = app.document().bodies();
        if (demo == QLatin1String("combine")) {
            (void)interaction.selectBody(bodies[0]->id(), false);
            (void)interaction.selectBody(bodies[1]->id(), true);
            return;
        }
        os::interact::PointerEvent edge;
        edge.position = interaction.camera().project({10, -10, 10});
        interaction.pointerPress(edge);
        interaction.pointerRelease(edge);
        interaction.setValueText("4");
        (void)interaction.commitOperation();
        interaction.setHistoryHighlight(bodies[0]->features().back()->id());
        return;
    }
    app.createBox(20);
    interaction.fitAll(false);
    const os::Vec2 top = interaction.camera().project({0, 0, 20});
    os::interact::PointerEvent click;
    click.position = top;
    if (demo == QLatin1String("pushpull") || demo == QLatin1String("committed")) {
        interaction.pointerPress(click);
        interaction.pointerRelease(click);
        interaction.setValueText("35"); // the new height (the chip shows 20 before)
        if (demo == QLatin1String("committed"))
            (void)interaction.commitOperation();
    } else if (demo == QLatin1String("fillet")) {
        click.position = interaction.camera().project({10, -10, 10});
        interaction.pointerPress(click);
        interaction.pointerRelease(click);
        interaction.setValueText("4");
    } else if (demo == QLatin1String("move")) {
        interaction.pointerPress(click);
        interaction.pointerRelease(click);
        interaction.pointerDoubleClick(click);
        interaction.setValueText("12");
    } else if (demo == QLatin1String("hover")) {
        click.button = os::interact::PointerButton::None;
        interaction.pointerMove(click);
    }
}

// --view: a standard view (iso, front, back, left, right, top, bottom) or
// "yaw,pitch" in degrees (yaw 0 looks from +X, -90 from the front; pitch 90
// from above), then everything framed: screenshots from any angle.
bool applyView(os::ui::AppController& app, const QString& view)
{
    auto& interaction = app.interaction();
    static const QMap<QString, os::StandardView> named{
        {QStringLiteral("iso"), os::StandardView::Isometric}, {QStringLiteral("front"), os::StandardView::Front},
        {QStringLiteral("back"), os::StandardView::Back},     {QStringLiteral("left"), os::StandardView::Left},
        {QStringLiteral("right"), os::StandardView::Right},   {QStringLiteral("top"), os::StandardView::Top},
        {QStringLiteral("bottom"), os::StandardView::Bottom}};
    if (const auto it = named.find(view); it != named.end()) {
        interaction.setStandardView(*it, false);
    } else {
        const QStringList parts = view.split(QLatin1Char(','));
        bool okYaw = false, okPitch = false;
        const double yaw = parts.size() == 2 ? parts[0].trimmed().toDouble(&okYaw) : 0.0;
        const double pitch = parts.size() == 2 ? parts[1].trimmed().toDouble(&okPitch) : 0.0;
        if (!okYaw || !okPitch)
            return false;
        interaction.setViewAngles(yaw * os::kPi / 180.0, pitch * os::kPi / 180.0, false);
    }
    interaction.fitAll(false);
    return true;
}

// ---- Remembered window -------------------------------------------------------------------

// Tracks the window's normal (not maximized) place while it runs, so the
// place saved on exit is the one to come back to even when it ends
// maximized. Read while the window still exists: once closed, its frame is
// gone and the frame geometry is just the client area.
struct WindowKeeper {
    QRect frame;
    QRect client;
    bool maximized = false;

    void record(const QQuickWindow* window)
    {
        if (!window->isVisible())
            return;
        maximized = window->windowStates() & Qt::WindowMaximized;
        if (window->windowStates() == Qt::WindowNoState) {
            frame = window->frameGeometry();
            client = window->geometry();
        }
    }
};

QList<QRect> availableScreens()
{
    QList<QRect> screens;
    if (QScreen* primary = QGuiApplication::primaryScreen())
        screens.append(primary->availableGeometry());
    for (QScreen* screen : QGuiApplication::screens())
        if (screen != QGuiApplication::primaryScreen())
            screens.append(screen->availableGeometry());
    return screens;
}

// Shows the window where it was last time (clamped to today's screens).
void showRemembered(QQuickWindow* window, const std::shared_ptr<WindowKeeper>& keeper)
{
    QSettings settings;
    const auto placement = os::ui::loadWindowPlacement(settings);
    if (placement) {
        const QRect frame = os::ui::fitToScreens(placement->frame, availableScreens(), window->minimumSize());
        const QRect client = os::ui::clientForFrame(*placement, frame);
        if (frame.isValid() && client.isValid()) {
            // By the client area: a frame position given before the window
            // exists is taken as the client's (it crept up a title bar per run).
            window->setGeometry(client);
            keeper->frame = frame;
            keeper->client = client;
            keeper->maximized = placement->maximized;
            OS_LOG(Info, App) << "window restored at " << client.x() << "," << client.y() << " " << client.width() << "x"
                              << client.height() << (placement->maximized ? " (maximized)" : "");
            if (placement->maximized)
                window->showMaximized();
            else
                window->show();
            return;
        }
    }
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect first = os::ui::firstWindowGeometry(window->size(), screen->availableGeometry(), window->minimumSize(), 40);
        if (first.isValid()) {
            window->setGeometry(first);
            OS_LOG(Info, App) << "the default window is larger than the screen; using " << first.width() << "x" << first.height();
        }
    }
    window->show();
}

void trackWindow(QQuickWindow* window, const std::shared_ptr<WindowKeeper>& keeper)
{
    auto* settle = new QTimer(window);
    settle->setSingleShot(true);
    settle->setInterval(300);
    QObject::connect(settle, &QTimer::timeout, window, [window, keeper] { keeper->record(window); });
    auto restart = [settle] { settle->start(); };
    QObject::connect(window, &QWindow::xChanged, settle, restart);
    QObject::connect(window, &QWindow::yChanged, settle, restart);
    QObject::connect(window, &QWindow::widthChanged, settle, restart);
    QObject::connect(window, &QWindow::heightChanged, settle, restart);
    QObject::connect(window, &QWindow::windowStateChanged, settle, restart);
    // Closing may still be refused (unsaved changes); recording is harmless.
    QObject::connect(window, &QQuickWindow::closing, window, [window, keeper] { keeper->record(window); });
}

void saveWindow(const WindowKeeper& keeper)
{
    if (!keeper.frame.isValid() || !keeper.client.isValid())
        return;
    QSettings settings;
    os::ui::saveWindowPlacement(settings, {keeper.frame, keeper.client, keeper.maximized});
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("OpenShape"));
    QGuiApplication::setApplicationName(QStringLiteral("OpenShape"));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(os::kAppVersion));
#if !defined(Q_OS_IOS) // iPadOS shows the bundle's icon (and would need the SVG plugin)
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/openshape/icons/openshape.svg")));
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // The UI is light only; without this a dark Windows theme turned the
    // menus black (they take the system palette).
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenShape - direct solid modeling"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption demoOption(QStringLiteral("demo"),
                                  QStringLiteral("Run a scripted demo scene (empty, hover, pushpull, committed, fillet, move, sketch, "
                                                 "sketchdone, extrude, bracket, revolve, arc, combine, history, rotate, mirror, pattern, "
                                                 "polygon, constraints, holes, home, enclosure; panels: help, about, preferences, modelpanel, viewmenu, "
                                                 "savename)."),
                                  QStringLiteral("name"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot to <file> after startup, then exit."),
                                        QStringLiteral("file"));
    QCommandLineOption viewOption(QStringLiteral("view"),
                                  QStringLiteral("With --demo: look from this direction, framed: iso, front, back, left, right, top, "
                                                 "bottom, or yaw,pitch in degrees (e.g. 30,20; yaw -90 is the front)."),
                                  QStringLiteral("view"));
    QCommandLineOption projectionOption(QStringLiteral("projection"),
                                        QStringLiteral("Start in this projection: perspective or orthographic (not remembered)."),
                                        QStringLiteral("kind"));
    QCommandLineOption acceptanceOption(QStringLiteral("acceptance"),
                                        QStringLiteral("Run the end-to-end acceptance script, write screenshots to <dir>, exit with the failure count."),
                                        QStringLiteral("dir"));
    QCommandLineOption touchOption(QStringLiteral("touch"),
                                   QStringLiteral("Start with the touch layout (as on a tablet): larger controls, Pen switch."));
    QCommandLineOption sizeOption(QStringLiteral("size"),
                                  QStringLiteral("Window size in logical pixels, e.g. 1180x820 (an 11-inch iPad in landscape)."),
                                  QStringLiteral("WxH"));
    QCommandLineOption safeAreaOption(QStringLiteral("safe-area"),
                                      QStringLiteral("Simulate a phone's safe-area insets in logical pixels, top,right,bottom,left "
                                                     "(e.g. 62,0,34,0: an iPhone 16 Pro in portrait; 0,62,21,62 in landscape)."),
                                      QStringLiteral("t,r,b,l"));
    QCommandLineOption appFolderOption(QStringLiteral("app-folder"),
                                       QStringLiteral("Save and export as on an iPhone or iPad: projects by name into <dir>, "
                                                      "exports into <dir>/Exports, no save dialogs."),
                                       QStringLiteral("dir"));
    QCommandLineOption scenarioOption(QStringLiteral("scenario"),
                                      QStringLiteral("With --acceptance: run only these scenarios (comma-separated, e.g. core,views)."),
                                      QStringLiteral("names"));
    QCommandLineOption dataDirOption(QStringLiteral("data-dir"),
                                     QStringLiteral("Keep settings, recovery copies and the log in <dir> instead of the user's folders."),
                                     QStringLiteral("dir"));
    QCommandLineOption simulateCrashOption(QStringLiteral("simulate-crash"),
                                           QStringLiteral("Test crash recovery: add a box, write its recovery copy, then crash."));
    QCommandLineOption simulateQuitOption(QStringLiteral("simulate-quit"),
                                          QStringLiteral("Test recovery: add a box, then quit without asking (as when iPadOS ends "
                                                         "the app); the unsaved box stays as a recovery copy."));
    parser.addOption(scenarioOption);
    parser.addOption(touchOption);
    parser.addOption(sizeOption);
    parser.addOption(safeAreaOption);
    parser.addOption(appFolderOption);
    parser.addOption(acceptanceOption);
    parser.addOption(demoOption);
    parser.addOption(viewOption);
    parser.addOption(projectionOption);
    parser.addOption(screenshotOption);
    parser.addOption(dataDirOption);
    parser.addOption(simulateCrashOption);
    QCommandLineOption simulateKernelFaultOption(
        QStringLiteral("simulate-kernel-fault"),
        QStringLiteral("Test: an access violation inside a modeling kernel call must become a failed step, not a crash."));
    parser.addOption(simulateQuitOption);
    parser.addOption(simulateKernelFaultOption);
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file to open."), QStringLiteral("[project]"));
    parser.process(application);

    // Automated runs (acceptance, demos, screenshots) never touch the user's
    // settings, recent files, window place or recovery copies: they get a
    // throw-away folder, removed when they end.
    const bool automated = parser.isSet(acceptanceOption) || parser.isSet(demoOption) || parser.isSet(screenshotOption);
    QString dataDir = parser.isSet(dataDirOption) ? QDir(parser.value(dataDirOption)).absolutePath() : QString();
    std::unique_ptr<QTemporaryDir> scratch;
    if (automated && dataDir.isEmpty()) {
        scratch = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-run-XXXXXX"));
        // Never fall back to the user's folders.
        dataDir = scratch->isValid() ? scratch->path()
                                     : QDir::tempPath() + QStringLiteral("/openshape-run-%1").arg(QCoreApplication::applicationPid());
    }
    if (!dataDir.isEmpty()) {
        QDir().mkpath(dataDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDir + QStringLiteral("/settings"));
    }
    const QString recoveryDir = (dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : dataDir)
                              + QStringLiteral("/recovery");

    // Logs go to the user's log file even in automated runs (so they can be
    // read afterwards), except with --data-dir.
    const QString logDir = logDirectory(parser.isSet(dataDirOption) ? dataDir : QString());
    installLogging(logDir);
    os::app::installCrashLogging(logDir + QStringLiteral("/openshape.log"));
    // OPENSHAPE_LOG=debug shows kernel/tessellation/recompute timings (PERFORMANCE).
    if (qEnvironmentVariable("OPENSHAPE_LOG").compare(QLatin1String("debug"), Qt::CaseInsensitive) == 0)
        os::setMinimumLogLevel(os::LogLevel::Debug);

    OS_LOG(Info, App) << "OpenShape " << os::kAppVersion << " starting";
    if (!dataDir.isEmpty())
        OS_LOG(Info, App) << "settings and recovery copies in " << QDir::toNativeSeparators(dataDir).toStdString();

    if (parser.isSet(simulateKernelFaultOption)) {
        const bool contained = os::app::simulateKernelFault();
        OS_LOG(Info, App) << (contained ? "simulated kernel fault: it became a failed step"
                                        : "simulated kernel fault: it was NOT contained");
        return contained ? 0 : 4;
    }
    if (parser.isSet(simulateCrashOption)) {
        // No window: an unsaved box, its recovery copy, then a real crash.
        os::ui::AppController controller;
        controller.startRecovery(recoveryDir);
        controller.createBox(20);
        controller.writeRecoveryCopy();
        OS_LOG(Info, App) << "simulating a crash; recovery copy " << controller.recoveryCopyFile().toStdString();
        os::app::simulateCrash();
    }
    if (parser.isSet(simulateQuitOption)) {
        // No window: an unsaved box, then the app is told to quit (no copy has
        // been written yet: edits settle for 3 s first).
        os::ui::AppController controller;
        controller.startRecovery(recoveryDir);
        controller.createBox(20);
        QTimer::singleShot(0, &controller, [] { QCoreApplication::exit(0); });
        const int result = QGuiApplication::exec();
        OS_LOG(Info, App) << "OpenShape exits normally";
        return result;
    }

    // Automated runs (acceptance, demos, screenshots) take turns: the
    // acceptance run moves the real mouse and needs keyboard focus, so two
    // at once (e.g. from parallel builds) would disturb each other. A lock
    // left by a crashed run is detected by its dead process; age alone never
    // makes it stale (a full acceptance run takes minutes).
    std::unique_ptr<QLockFile> automationLock;
    if (automated) {
        automationLock = std::make_unique<QLockFile>(QDir::tempPath() + QStringLiteral("/openshape-automation.lock"));
        automationLock->setStaleLockTime(0);
        if (!automationLock->tryLock(0)) {
            OS_LOG(Info, App) << "waiting for another automated OpenShape run to finish";
            if (!automationLock->tryLock(15 * 60 * 1000)) {
                OS_LOG(Error, App) << "another automated OpenShape run is still going after 15 minutes; giving up";
                return 3;
            }
        }
    }

    os::ui::AppController controller;
    controller.startRecovery(recoveryDir);
    if (parser.isSet(touchOption))
        controller.setTouchMode(true);
    if (parser.isSet(projectionOption)) {
        const QString kind = parser.value(projectionOption);
        if (kind == QLatin1String("perspective") || kind == QLatin1String("orthographic"))
            controller.interaction().setProjection(kind == QLatin1String("perspective") ? os::Camera::Projection::Perspective
                                                                                       : os::Camera::Projection::Orthographic);
        else
            OS_LOG(Warning, App) << "--projection expects perspective or orthographic";
    }
    // Home at launch without a file (never in automated runs, which start
    // from an empty document; the "home" demo shows it).
    if (!automated && parser.positionalArguments().isEmpty())
        controller.setHomeVisible(true);
    if (parser.isSet(appFolderOption))
        controller.setAppFolder(parser.value(appFolderOption));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new os::ui::ThumbnailProvider); // the engine owns it
    QVariantMap initialProperties{{QStringLiteral("app"), QVariant::fromValue(&controller)}};
    if (parser.isSet(safeAreaOption)) {
        // Main.qml keeps the controls out of these insets as it does out of
        // a real phone's (SafeArea), and shades them.
        const QStringList parts = parser.value(safeAreaOption).split(QLatin1Char(','));
        QVariantList insets;
        for (const QString& part : parts) {
            bool ok = false;
            const double value = part.trimmed().toDouble(&ok);
            if (ok && value >= 0)
                insets.append(value);
        }
        if (parts.size() == 4 && insets.size() == 4)
            initialProperties.insert(QStringLiteral("simulatedSafeArea"), insets);
        else
            OS_LOG(Warning, App) << "--safe-area expects four numbers, top,right,bottom,left, e.g. 62,0,34,0";
    }
    engine.setInitialProperties(initialProperties);
    engine.loadFromModule("OpenShape", "Main");
    if (engine.rootObjects().isEmpty()) {
        OS_LOG(Error, App) << "failed to load the user interface";
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    // The window comes back where it was, except in automated runs, with
    // --size, and on tablets (always full screen).
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    const bool rememberWindow = false;
#else
    const bool rememberWindow = !automated && !parser.isSet(sizeOption);
#endif
    auto keeper = std::make_shared<WindowKeeper>();
    if (parser.isSet(sizeOption)) {
        const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
        if (wh.size() == 2 && wh[0].toInt() > 0 && wh[1].toInt() > 0) {
            window->setMinimumSize({}); // allow phone-sized checks below the usual minimum
            window->resize(wh[0].toInt(), wh[1].toInt());
        } else {
            OS_LOG(Warning, App) << "--size expects WxH, e.g. 1180x820";
        }
    }
    if (rememberWindow) {
        showRemembered(window, keeper);
        trackWindow(window, keeper);
    } else {
        window->show();
    }

    if (!parser.positionalArguments().isEmpty())
        controller.openProject(QUrl::fromLocalFile(parser.positionalArguments().constFirst()));
    // Work left by a crash: offered in a dialog (not in automated runs).
    if (!automated)
        controller.checkForRecovery();

    if (parser.isSet(acceptanceOption)) {
        auto* runner = new os::app::AcceptanceRunner(window, &controller, parser.value(acceptanceOption), &controller);
        if (parser.isSet(scenarioOption))
            runner->setScenarioFilter(parser.value(scenarioOption).split(QLatin1Char(','), Qt::SkipEmptyParts));
        runner->start();
    } else if (parser.isSet(demoOption) || parser.isSet(screenshotOption)) {
        const QString demo = parser.value(demoOption);
        const QString shot = parser.value(screenshotOption);
        const QString view = parser.value(viewOption);
        // Wait for the first frames so the viewport knows its size.
        QTimer::singleShot(600, &controller, [&controller, window, demo, view, dataDir] {
            if (demo.isEmpty())
                return;
            // Seen from --view once the scene is built (below).
            const auto lookFromView = qScopeGuard([&controller, &view] {
                if (!view.isEmpty() && !applyView(controller, view))
                    OS_LOG(Warning, App) << "--view expects a view name (iso, front, top, ...) or yaw,pitch in degrees";
            });
            // Panels to look at (layout checks at phone sizes): the help card,
            // About, Preferences; the compact layout's Model panel and View menu.
            static const QMap<QString, QPair<QString, const char*>> panels{
                {QStringLiteral("help"), {QStringLiteral("combine"), "helpOverlay"}},
                {QStringLiteral("about"), {QStringLiteral("empty"), "aboutOverlay"}},
                {QStringLiteral("preferences"), {QStringLiteral("empty"), "preferencesOverlay"}},
                {QStringLiteral("modelpanel"), {QStringLiteral("history"), "historyOpen"}},
                {QStringLiteral("viewmenu"), {QStringLiteral("combine"), "viewMenuOpen"}},
                {QStringLiteral("savename"), {QStringLiteral("bracket"), "saveNamePrompt"}},
            };
            const auto panel = panels.find(demo);
            runDemo(controller, panel == panels.end() ? demo : panel->first, dataDir);
            // Previews compute on a worker thread: show the scene's before the screenshot.
            (void)controller.interaction().waitForPreview();
            if (panel == panels.end())
                return;
            if (auto* item = window->findChild<QQuickItem*>(QString::fromLatin1(panel->second)))
                item->setVisible(true);
            else
                window->setProperty(panel->second, true); // the compact layout's switches (Main.qml)
        });
        if (!shot.isEmpty()) {
            QTimer::singleShot(1600, window, [window, shot] {
                const QImage image = window->grabWindow();
                const bool ok = image.save(shot);
                OS_LOG(Info, App) << "screenshot " << shot.toStdString() << (ok ? " saved" : " FAILED") << " ("
                                  << image.width() << "x" << image.height() << ")";
                QCoreApplication::exit(ok ? 0 : 2);
            });
        }
    }

    const int result = QGuiApplication::exec();
    if (rememberWindow)
        saveWindow(*keeper);
    OS_LOG(Info, App) << "OpenShape exits normally";
    return result;
}
