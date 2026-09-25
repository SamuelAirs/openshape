#include "app/AcceptanceRunner.h"

#include "core/Log.h"
#include "geometry/Modeling.h"
#include "interaction/Operation.h"
#include "ui/AppController.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QCursor>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <qpa/qwindowsysteminterface.h>

namespace os::app {

namespace {
constexpr int kStepDelayMs = 160;
} // namespace

AcceptanceRunner::AcceptanceRunner(QQuickWindow* window, ui::AppController* app, QString outputDir, QObject* parent)
    : QObject(parent), window_(window), app_(app), outputDir_(std::move(outputDir))
{
    QDir().mkpath(outputDir_);
}

// ---- Input injection ----------------------------------------------------------------

void AcceptanceRunner::mouseMove(QPointF p, Qt::MouseButtons held)
{
    QCursor::setPos(window_->mapToGlobal(p.toPoint()));
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window_, p, window_->mapToGlobal(p), held, Qt::NoButton, QEvent::MouseMove);
}

void AcceptanceRunner::mousePress(QPointF p, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window_, p, window_->mapToGlobal(p), button, button, QEvent::MouseButtonPress, mods);
}

void AcceptanceRunner::mouseRelease(QPointF p, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window_, p, window_->mapToGlobal(p), Qt::NoButton, button, QEvent::MouseButtonRelease, mods);
}

void AcceptanceRunner::click(QPointF p, Qt::KeyboardModifiers mods)
{
    mouseMove(p);
    mousePress(p, Qt::LeftButton, mods);
    mouseRelease(p, Qt::LeftButton, mods);
}

void AcceptanceRunner::drag(QPointF from, QPointF to, int steps)
{
    mouseMove(from);
    mousePress(from);
    for (int i = 1; i <= steps; ++i)
        mouseMove(from + (to - from) * (double(i) / steps), Qt::LeftButton);
    mouseRelease(to);
}

void AcceptanceRunner::key(int k, Qt::KeyboardModifiers mods, const QString& text)
{
    // Platform plugins offer a key to the shortcut map first, then deliver it.
    if (QWindowSystemInterface::handleShortcutEvent(window_, 0, k, mods, 0, 0, 0, text))
        return;
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(window_, QEvent::KeyPress, k, mods, text);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(window_, QEvent::KeyRelease, k, mods, text);
}

void AcceptanceRunner::type(const QString& text)
{
    for (const QChar c : text) {
        int k = c.unicode();
        if (c.isDigit())
            k = Qt::Key_0 + c.digitValue();
        else if (c == QLatin1Char('.'))
            k = Qt::Key_Period;
        key(k, Qt::NoModifier, QString(c));
    }
}

namespace {
// Depth-first search of the visual item tree. Needed for delegates created by
// Repeater/ListView, which are not QObject children of the window.
QQuickItem* findVisualItem(QQuickItem* root, const QString& objectName)
{
    if (!root)
        return nullptr;
    if (root->objectName() == objectName)
        return root;
    for (QQuickItem* child : root->childItems())
        if (QQuickItem* found = findVisualItem(child, objectName))
            return found;
    return nullptr;
}
} // namespace

bool AcceptanceRunner::clickItem(const QString& objectName)
{
    // Declared items are QObject children of the window; generated delegates
    // are only reachable through the visual tree.
    auto* item = window_->findChild<QQuickItem*>(objectName);
    if (!item)
        item = findVisualItem(window_->contentItem(), objectName);
    if (!item || !item->isVisible() || !item->isEnabled()) {
        OS_LOG(Warning, App) << "clickItem: '" << objectName.toStdString() << "' "
                             << (!item ? "not found" : !item->isVisible() ? "not visible" : "disabled");
        return false;
    }
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    OS_LOG(Info, App) << "clickItem: '" << objectName.toStdString() << "' at " << center.x() << "," << center.y();
    click(center);
    return true;
}

// ---- Measurements -----------------------------------------------------------------

QPointF AcceptanceRunner::screenPoint(double x, double y, double z) const
{
    const Vec2 p = app_->interaction().camera().project({x, y, z});
    return {p.x, p.y};
}

double AcceptanceRunner::bodyHeight() const
{
    const auto& bodies = app_->document().bodies();
    return bodies.empty() ? -1.0 : geom::boundingBox(bodies.front()->shape()).size().z;
}

double AcceptanceRunner::bodyVolume() const
{
    const auto& bodies = app_->document().bodies();
    return bodies.empty() ? -1.0 : geom::volume(bodies.front()->shape());
}

void AcceptanceRunner::check(bool condition, const QString& description, const QString& actual)
{
    ++checks_;
    if (!condition)
        ++failures_;
    OS_LOG(Info, App) << (condition ? "[PASS] " : "[FAIL] ") << description.toStdString()
                      << (actual.isEmpty() ? "" : " (" + actual.toStdString() + ")");
}

void AcceptanceRunner::screenshot(const QString& name)
{
    const QString path = outputDir_ + QLatin1Char('/') + name + QStringLiteral(".png");
    const bool ok = window_->grabWindow().save(path);
    OS_LOG(Info, App) << "screenshot " << path.toStdString() << (ok ? "" : " FAILED");
}

// ---- Script -----------------------------------------------------------------------------

void AcceptanceRunner::start()
{
    auto& in = app_->interaction();
    auto num = [](double v) { return QString::number(v, 'f', 6); };
    auto topFaceSelected = [this] {
        const auto& sel = app_->interaction().selection();
        if (sel.size() != 1 || sel.items()[0].kind != sel::SelectionKind::Face)
            return false;
        const auto info = geom::faceInfo(app_->document().body(sel.items()[0].bodyId)->shape(), sel.items()[0].index);
        return info && info->normal.z > 0.999;
    };

    steps_ = {
        // 1-2. Launch, create a 20 mm cube with the "B" shortcut.
        [=, this] {
            check(app_->bodyCount() == 0, "starts with an empty document");
            key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        },
        [] {}, [] {}, [] {}, // let the fit animation finish
        [=, this] {
            check(app_->bodyCount() == 1, "B creates a body");
            check(std::abs(bodyHeight() - 20.0) < 1e-9, "cube height is 20 mm", num(bodyHeight()));
            screenshot(QStringLiteral("01_cube"));
        },
        // 3. Orbit by dragging empty space.
        [=, this, &in] {
            const double yaw = in.camera().yaw;
            drag({window_->width() - 160.0, 180.0}, {window_->width() - 60.0, 230.0});
            check(std::abs(in.camera().yaw - yaw) > 1e-3, "dragging empty space orbits the view");
            check(in.selection().empty(), "orbit drag does not select");
            app_->setView(QStringLiteral("iso"));
        },
        [] {}, [] {}, [] {},
        // 4-5. Hover the top face.
        [=, this, &in] {
            mouseMove(screenPoint(0, 0, 20));
            const auto& hover = in.hover();
            bool isTop = hover.kind == sel::PickKind::Face;
            if (isTop) {
                const auto info = geom::faceInfo(app_->document().body(hover.bodyId)->shape(), hover.index);
                isTop = info && info->normal.z > 0.999;
            }
            check(isTop, "hovering the top face highlights it");
            screenshot(QStringLiteral("02_hover_top_face"));
        },
        // 6-8. Click selects it; the push/pull arrow appears.
        [=, this, &in] {
            click(screenPoint(0, 0, 20));
            check(topFaceSelected(), "clicking selects the top face");
            check(app_->operationActive() && app_->operationTitle() == QStringLiteral("Push/Pull"),
                  "selected planar face shows the push/pull manipulator", app_->operationTitle());
            check(in.renderScene().arrows.size() == 1, "one arrow is drawn");
            screenshot(QStringLiteral("03_face_selected"));
        },
        // 9-10. Drag the arrow upward: live preview, document unchanged.
        [=, this, &in] {
            const auto* op = in.operation();
            const Vec3 anchor = op->anchor();
            const double px = in.camera().pixelSize(anchor);
            const interact::ArrowStyle style;
            const Vec3 grab = anchor + op->manipulator().direction() * ((style.gapPx + style.shaftPx * 0.6) * px);
            const QPointF from = screenPoint(grab.x, grab.y, grab.z);
            drag(from, from + QPointF(0, -90));
            check(in.operation() && in.operation()->value() > 1.0, "dragging the arrow changes the value",
                  in.operation() ? num(in.operation()->value()) : QStringLiteral("no operation"));
            check(in.operation() && in.operation()->hasPreview(), "the preview updates while dragging");
            check(std::abs(bodyHeight() - 20.0) < 1e-9, "preview does not modify the document", num(bodyHeight()));
            screenshot(QStringLiteral("04_drag_preview"));
        },
        // 11. Type an exact value (typing goes straight to the value field).
        [=, this, &in] {
            type(QStringLiteral("15"));
            check(in.operation() && std::abs(in.operation()->value() - 15.0) < 1e-12, "typing 15 sets the value to 15 mm",
                  in.operation() ? num(in.operation()->value()) : QString());
            screenshot(QStringLiteral("05_typed_15"));
        },
        // 12. Enter commits: exactly 35 mm.
        [=, this] {
            key(Qt::Key_Return);
            check(std::abs(bodyHeight() - 35.0) < 1e-9, "Enter commits: height is 35 mm", num(bodyHeight()));
            check(std::abs(bodyVolume() - 14000.0) < 1e-6, "volume is 14000 mm^3", num(bodyVolume()));
            check(topFaceSelected(), "the pushed face stays selected");
            screenshot(QStringLiteral("06_committed_35mm"));
        },
        // 13-14. Undo.
        [=, this] {
            key(Qt::Key_Z, Qt::ControlModifier);
            check(std::abs(bodyHeight() - 20.0) < 1e-9, "Ctrl+Z restores 20 mm", num(bodyHeight()));
        },
        // 15-16. Redo.
        [=, this] {
            key(Qt::Key_Y, Qt::ControlModifier);
            check(std::abs(bodyHeight() - 35.0) < 1e-9, "Ctrl+Y returns to 35 mm", num(bodyHeight()));
            screenshot(QStringLiteral("07_redo_35mm"));
        },
        // Beyond M0: fillet one vertical edge by typing a radius.
        [=, this, &in] {
            key(Qt::Key_Escape);
            check(in.selection().empty(), "Escape clears the selection");
            click(screenPoint(10, -10, 17.5));
            check(in.selection().size() == 1 && in.selection().items()[0].kind == sel::SelectionKind::Edge,
                  "clicking an edge selects it");
            check(app_->operationTitle() == QStringLiteral("Fillet"), "edge selection offers a fillet",
                  app_->operationTitle());
        },
        [=, this] {
            type(QStringLiteral("3"));
            key(Qt::Key_Return);
            const double expected = 20.0 * 20.0 * 35.0 - (9.0 - kPi * 9.0 / 4.0) * 35.0;
            check(std::abs(bodyVolume() - expected) < 1e-3, "3 mm fillet gives the exact volume", num(bodyVolume()));
            screenshot(QStringLiteral("08_fillet_3mm"));
        },
        // Save, start over, reopen, export.
        [=, this] {
            const double volume = bodyVolume();
            const QString project = outputDir_ + QStringLiteral("/acceptance.openshape");
            QFile::remove(project);
            check(app_->saveProjectAs(QUrl::fromLocalFile(project)), "project saves");
            app_->newDocument();
            check(app_->bodyCount() == 0, "new document is empty");
            check(app_->openProject(QUrl::fromLocalFile(project)), "project reopens");
            check(app_->bodyCount() == 1 && std::abs(bodyVolume() - volume) < 1e-6, "reopened model is identical",
                  num(bodyVolume()));
            const QString step = outputDir_ + QStringLiteral("/acceptance.step");
            const QString stl = outputDir_ + QStringLiteral("/acceptance.stl");
            check(app_->exportStep(QUrl::fromLocalFile(step)) && QFileInfo(step).size() > 1000, "STEP export");
            check(app_->exportStl(QUrl::fromLocalFile(stl)) && QFileInfo(stl).size() > 84, "STL export");
            key(Qt::Key_Z, Qt::ControlModifier);
            check(app_->bodyCount() == 1, "undo history starts fresh after opening");
        },
        [] {}, [] {},
        [=, this] { screenshot(QStringLiteral("09_reopened")); },

        // ================= Milestone 1: the printable bracket =================
        // New document; K starts a sketch on the ground plane.
        [=, this] {
            app_->newDocument();
            check(app_->bodyCount() == 0, "new document for the bracket");
            key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            check(app_->sketchMode(), "K starts a sketch");
            check(app_->sketchTool() == QStringLiteral("rectangle"), "a new sketch starts with the rectangle tool",
                  app_->sketchTool());
        },
        [] {}, [] {}, [] {}, // camera turns to face the sketch plane
        // Rectangle: click the origin, move, type 60 Tab 30 Enter.
        [=, this, &in] {
            check(std::abs(in.camera().forward().z + 1.0) < 1e-6, "view looks straight down at the XY sketch");
            click(screenPoint(0, 0, 0));
            mouseMove(screenPoint(40, 22, 0));
            check(app_->sketchDrawing(), "first click starts the rectangle");
            type(QStringLiteral("60"));
            key(Qt::Key_Tab);
            type(QStringLiteral("30"));
            key(Qt::Key_Return);
            check(!app_->sketchDrawing(), "Enter completes the rectangle");
            check(app_->sketchStatus() == QStringLiteral("Fully defined"), "rectangle anchored at the origin is fully defined",
                  app_->sketchStatus());
            screenshot(QStringLiteral("10_sketch_rectangle"));
        },
        [=, this] {
            check(clickItem(QStringLiteral("finishSketchButton")), "Finish sketch button");
            check(!app_->sketchMode(), "finishing leaves sketch mode");
        },
        [] {}, [] {}, [] {},
        // Select the profile and extrude 5 mm.
        [=, this, &in] {
            click(screenPoint(30, 15, 0));
            check(in.selection().size() == 1 && in.selection().items()[0].kind == sel::SelectionKind::SketchProfile,
                  "clicking inside the rectangle selects the profile");
            check(app_->operationTitle() == QStringLiteral("Extrude"), "profile selection offers extrude", app_->operationTitle());
            type(QStringLiteral("5"));
            key(Qt::Key_Return);
            check(app_->bodyCount() == 1, "extrusion creates a body");
            check(std::abs(bodyVolume() - 9000.0) < 1e-6, "plate volume is 60 x 30 x 5", num(bodyVolume()));
        },
        [] {},
        // Sketch two 6 mm holes on the top face.
        [=, this] {
            click(screenPoint(30, 15, 5));
            key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            check(app_->sketchMode(), "K on a selected face starts a sketch on it");
        },
        [] {}, [] {}, [] {},
        [=, this] {
            key(Qt::Key_C, Qt::NoModifier, QStringLiteral("c"));
            check(app_->sketchTool() == QStringLiteral("circle"), "C picks the circle tool", app_->sketchTool());
            for (double x : {10.0, 50.0}) {
                click(screenPoint(x, 15, 5));
                mouseMove(screenPoint(x + 2, 15, 5));
                type(QStringLiteral("6"));
                key(Qt::Key_Return);
            }
            const auto* session = app_->interaction().sketchSession();
            check(session && session->sketch().circles().size() == 2, "two circles drawn");
            screenshot(QStringLiteral("11_sketch_holes"));
            check(clickItem(QStringLiteral("finishSketchButton")), "Finish sketch button (holes)");
        },
        [] {}, [] {}, [] {},
        // Select both hole profiles, push down (cut) and choose "Through all".
        [=, this, &in] {
            click(screenPoint(10, 15, 5));
            click(screenPoint(50, 15, 5), Qt::ShiftModifier);
            check(in.selection().size() == 2, "shift-click adds the second hole profile");
            type(QStringLiteral("-5"));
        },
        [=, this] {
            check(clickItem(QStringLiteral("action_throughAll")), "cut offers Through all");
            const auto* op = dynamic_cast<const interact::ExtrudeOperation*>(app_->interaction().operation());
            check(op && op->throughAll() && op->mode() == doc::ExtrudeMode::Cut, "through-all cut armed");
            key(Qt::Key_Return);
            const double expected = 9000.0 - 2 * kPi * 9.0 * 5.0;
            check(std::abs(bodyVolume() - expected) < 1e-3, "two 6 mm holes cut through the plate", num(bodyVolume()));
            check(!app_->document().bodies().empty() && geom::isValid(app_->document().bodies().front()->shape()),
                  "bracket is a valid solid");
        },
        [] {}, [] {}, [] {},
        [=, this] {
            screenshot(QStringLiteral("12_bracket"));
            const QString step = outputDir_ + QStringLiteral("/bracket.step");
            const QString stl = outputDir_ + QStringLiteral("/bracket.stl");
            check(app_->exportStep(QUrl::fromLocalFile(step)) && QFileInfo(step).size() > 1000, "bracket STEP export");
            check(app_->exportStl(QUrl::fromLocalFile(stl)) && QFileInfo(stl).size() > 84, "bracket STL export");
            const QString threeMf = outputDir_ + QStringLiteral("/bracket.3mf");
            check(app_->export3mf(QUrl::fromLocalFile(threeMf)) && QFileInfo(threeMf).size() > 500, "bracket 3MF export");
            const QString project = outputDir_ + QStringLiteral("/bracket.openshape");
            QFile::remove(project);
            check(app_->saveProjectAs(QUrl::fromLocalFile(project)), "bracket project saves");
        },
        // Parametric edit through the history panel: make the plate 8 mm.
        [=, this] {
            const doc::Body& body = *app_->document().bodies().front();
            const QString id = QString::fromStdString(body.features().front()->id().toString());
            check(clickItem(QStringLiteral("historyRow_") + id), "history row of the plate extrusion");
        },
        [=, this] {
            const doc::Body& body = *app_->document().bodies().front();
            const QString id = QString::fromStdString(body.features().front()->id().toString());
            check(clickItem(QStringLiteral("historyParam_") + id + QStringLiteral("_distance")), "distance field in the history");
            type(QStringLiteral("8"));
            key(Qt::Key_Return);
        },
        [] {}, [] {},
        [=, this] {
            const double expected = (1800.0 - 2 * kPi * 9.0) * 8.0;
            check(std::abs(bodyHeight() - 8.0) < 1e-6, "plate is now 8 mm thick", num(bodyHeight()));
            check(std::abs(bodyVolume() - expected) < 1e-3, "holes still go through after the edit", num(bodyVolume()));
            check(!app_->document().bodies().front()->hasFailures(), "no failed steps after the edit");
            screenshot(QStringLiteral("13_edited_8mm"));
            key(Qt::Key_Z, Qt::ControlModifier);
            check(std::abs(bodyHeight() - 5.0) < 1e-6, "undo restores 5 mm", num(bodyHeight()));
        },
    };
    QTimer::singleShot(400, this, &AcceptanceRunner::runNext);
}

void AcceptanceRunner::runNext()
{
    if (next_ < steps_.size()) {
        steps_[next_++]();
        QTimer::singleShot(kStepDelayMs, this, &AcceptanceRunner::runNext);
        return;
    }
    OS_LOG(Info, App) << "acceptance: " << (checks_ - failures_) << "/" << checks_ << " checks passed";
    QCoreApplication::exit(failures_);
}

} // namespace os::app
