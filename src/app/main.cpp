// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "app/AcceptanceRunner.h"
#include "app/CrashLog.h"
#include "core/Log.h"
#include "geometry/Modeling.h"
#include "ui/AppController.h"
#include "ui/AppSettings.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLockFile>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtGui/QStyleHints>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>

#include <cstdio>
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

// Scripted demo used for screenshots and smoke tests: builds the Milestone 0
// state (cube, top face selected, push/pull preview to a 35 mm height).
void runDemo(os::ui::AppController& app, const QString& demo)
{
    auto& interaction = app.interaction();
    interaction.fitAll(false);
    if (demo == QLatin1String("empty"))
        return;
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
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
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
                                                 "sketchdone, extrude, bracket, revolve, arc, combine, history, rotate, mirror, pattern)."),
                                  QStringLiteral("name"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot to <file> after startup, then exit."),
                                        QStringLiteral("file"));
    QCommandLineOption acceptanceOption(QStringLiteral("acceptance"),
                                        QStringLiteral("Run the end-to-end acceptance script, write screenshots to <dir>, exit with the failure count."),
                                        QStringLiteral("dir"));
    QCommandLineOption touchOption(QStringLiteral("touch"),
                                   QStringLiteral("Start with the touch layout (as on a tablet): larger controls, Pen switch."));
    QCommandLineOption sizeOption(QStringLiteral("size"),
                                  QStringLiteral("Window size in logical pixels, e.g. 1180x820 (an 11-inch iPad in landscape)."),
                                  QStringLiteral("WxH"));
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
    parser.addOption(acceptanceOption);
    parser.addOption(demoOption);
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

    OS_LOG(Info, App) << "OpenShape 0.1.0 starting";
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
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&controller)}});
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
        // Wait for the first frames so the viewport knows its size.
        QTimer::singleShot(600, &controller, [&controller, demo] {
            if (!demo.isEmpty())
                runDemo(controller, demo);
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
