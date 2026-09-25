#include "app/AcceptanceRunner.h"
#include "core/Log.h"
#include "geometry/Modeling.h"
#include "ui/AppController.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>

#include <cstdio>
#include <mutex>

Q_IMPORT_QML_PLUGIN(OpenShapePlugin)

namespace {

// Log sink: stderr plus a rolling file in the user's app-data folder.
void installLogging()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
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
// state (cube, top face selected, push/pull preview at +15 mm).
void runDemo(os::ui::AppController& app, const QString& demo)
{
    auto& interaction = app.interaction();
    interaction.fitAll(false);
    if (demo == QLatin1String("empty"))
        return;
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
    app.createBox(20);
    interaction.fitAll(false);
    const os::Vec2 top = interaction.camera().project({0, 0, 20});
    os::interact::PointerEvent click;
    click.position = top;
    if (demo == QLatin1String("pushpull") || demo == QLatin1String("committed")) {
        interaction.pointerPress(click);
        interaction.pointerRelease(click);
        interaction.setValueText("15");
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

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("OpenShape"));
    QGuiApplication::setApplicationName(QStringLiteral("OpenShape"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/openshape/icons/openshape.svg")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    installLogging();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenShape - direct solid modeling"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Run a scripted demo scene (empty, hover, pushpull, committed, fillet)."),
                                  QStringLiteral("name"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot to <file> after startup, then exit."),
                                        QStringLiteral("file"));
    QCommandLineOption acceptanceOption(QStringLiteral("acceptance"),
                                        QStringLiteral("Run the end-to-end acceptance script, write screenshots to <dir>, exit with the failure count."),
                                        QStringLiteral("dir"));
    parser.addOption(acceptanceOption);
    parser.addOption(demoOption);
    parser.addOption(screenshotOption);
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file to open."), QStringLiteral("[project]"));
    parser.process(application);

    OS_LOG(Info, App) << "OpenShape 0.1.0 starting";

    os::ui::AppController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("OpenShape", "Main");
    if (engine.rootObjects().isEmpty()) {
        OS_LOG(Error, App) << "failed to load the user interface";
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());

    if (!parser.positionalArguments().isEmpty())
        controller.openProject(QUrl::fromLocalFile(parser.positionalArguments().constFirst()));

    if (parser.isSet(acceptanceOption)) {
        auto* runner = new os::app::AcceptanceRunner(window, &controller, parser.value(acceptanceOption), &controller);
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

    return QGuiApplication::exec();
}
