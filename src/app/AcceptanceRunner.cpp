// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
#include <QtGui/QPointingDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

// QWindowSystemInterface takes positions in native (device) pixels, as the
// platform plugin delivers them; the script works in logical pixels. On a
// display scaled to 150 % the unconverted clicks landed at two thirds of
// their target (2026-09-26, a second monitor).
QPointF AcceptanceRunner::nativeLocal(QPointF p) const
{
    return QHighDpi::toNativeLocalPosition(p, window_);
}

QPointF AcceptanceRunner::nativeGlobal(QPointF p) const
{
    return QHighDpi::toNativeGlobalPosition(window_->mapToGlobal(p), window_);
}

void AcceptanceRunner::mouseMove(QPointF p, Qt::MouseButtons held)
{
    QCursor::setPos(window_->mapToGlobal(p.toPoint())); // logical: Qt converts
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window_, nativeLocal(p), nativeGlobal(p), held, Qt::NoButton, QEvent::MouseMove);
}

void AcceptanceRunner::mousePress(QPointF p, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window_, nativeLocal(p), nativeGlobal(p), button, button, QEvent::MouseButtonPress, mods);
}

void AcceptanceRunner::mouseRelease(QPointF p, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window_, nativeLocal(p), nativeGlobal(p), Qt::NoButton, button, QEvent::MouseButtonRelease, mods);
}

void AcceptanceRunner::click(QPointF p, Qt::KeyboardModifiers mods)
{
    lastClick_ = p;
    hasLastClick_ = true;
    mouseMove(p);
    mousePress(p, Qt::LeftButton, mods);
    mouseRelease(p, Qt::LeftButton, mods);
}

void AcceptanceRunner::wheel(QPointF p, int notches)
{
    mouseMove(p);
    // Native pixels, like the mouse (nativeLocal).
    QWindowSystemInterface::handleWheelEvent(window_, nativeLocal(p), nativeGlobal(p), QPoint(), QPoint(0, 120 * notches));
    QWindowSystemInterface::flushWindowSystemEvents(); // delivered now, like the other input helpers
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

void AcceptanceRunner::touchTap(const QList<QPointF>& points)
{
    static QPointingDevice* device = [] {
        auto* d = new QPointingDevice(QStringLiteral("OpenShape acceptance touch"), 4242, QInputDevice::DeviceType::TouchScreen,
                                      QPointingDevice::PointerType::Finger,
                                      QInputDevice::Capability::Position | QInputDevice::Capability::Area, 10, 0);
        QWindowSystemInterface::registerInputDevice(d);
        return d;
    }();
    auto frame = [&](QEventPoint::State state) {
        QList<QWindowSystemInterface::TouchPoint> list;
        int id = 1;
        for (const QPointF& p : points) {
            QWindowSystemInterface::TouchPoint tp;
            tp.id = id++;
            tp.state = state;
            tp.area = QRectF(nativeGlobal(p) - QPointF(3, 3), QSizeF(6, 6)); // native pixels, like the mouse
            tp.pressure = state == QEventPoint::State::Released ? 0 : 1;
            list.append(tp);
        }
        return list;
    };
    QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(window_, device,
                                                                                         frame(QEventPoint::State::Pressed));
    QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(window_, device,
                                                                                         frame(QEventPoint::State::Released));
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

QQuickItem* findVisibleVisualItem(QQuickItem* root, const QString& objectName)
{
    if (!root || !root->isVisible())
        return nullptr;
    if (root->objectName() == objectName)
        return root;
    for (QQuickItem* child : root->childItems())
        if (QQuickItem* found = findVisibleVisualItem(child, objectName))
            return found;
    return nullptr;
}
} // namespace

bool AcceptanceRunner::clickItem(const QString& objectName, Qt::KeyboardModifiers mods)
{
    // Declared items are QObject children of the window; generated delegates
    // are only reachable through the visual tree.
    auto* item = findItem(objectName);
    if (!item || !item->isVisible() || !item->isEnabled()) {
        OS_LOG(Warning, App) << "clickItem: '" << objectName.toStdString() << "' "
                             << (!item ? "not found" : !item->isVisible() ? "not visible" : "disabled");
        return false;
    }
    // Buttons created by the last input (e.g. the actions of a new selection)
    // are not laid out until the next frame: lay out their rows now, from the
    // top down, or the click lands wherever the row stacked them (on Delete).
    std::vector<QQuickItem*> chain;
    for (QQuickItem* p = item; p; p = p->parentItem())
        chain.push_back(p);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        (*it)->ensurePolished();
    // Scroll it into view (the tool palette scrolls in short windows; the
    // compact layout's tool strip and action rows scroll sideways).
    for (QQuickItem* p : chain) {
        if (p == item || !p->inherits("QQuickFlickable"))
            continue;
        auto* content = p->property("contentItem").value<QQuickItem*>();
        if (!content)
            continue;
        const QPointF topLeft = item->mapToItem(content, QPointF(0, 0));
        double contentY = p->property("contentY").toDouble();
        if (topLeft.y() < contentY)
            contentY = topLeft.y();
        else if (topLeft.y() + item->height() > contentY + p->height())
            contentY = topLeft.y() + item->height() - p->height();
        p->setProperty("contentY", contentY);
        double contentX = p->property("contentX").toDouble();
        if (topLeft.x() < contentX)
            contentX = topLeft.x();
        else if (topLeft.x() + item->width() > contentX + p->width())
            contentX = topLeft.x() + item->width() - p->width();
        p->setProperty("contentX", contentX);
    }
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    OS_LOG(Info, App) << "clickItem: '" << objectName.toStdString() << "' at " << center.x() << "," << center.y();
    click(center, mods);
    return true;
}

// ---- Measurements -----------------------------------------------------------------

QPointF AcceptanceRunner::screenPoint(double x, double y, double z) const
{
    const Vec2 p = app_->interaction().camera().project({x, y, z});
    return {p.x, p.y};
}

const doc::Body& AcceptanceRunner::body(std::size_t index)
{
    const auto& bodies = app_->document().bodies();
    if (index >= bodies.size()) {
        check(false, QStringLiteral("body %1 exists (an earlier failure changed the model; skipping the rest of '%2')")
                         .arg(index + 1)
                         .arg(scenario_));
        throw AbortScenario{};
    }
    return *bodies[index];
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
    ++scenarioChecks_;
    if (!condition) {
        ++failures_;
        ++scenarioFailures_;
    }
    OS_LOG(Info, App) << (condition ? "[PASS] " : "[FAIL] ") << description.toStdString()
                      << (actual.isEmpty() ? "" : " (" + actual.toStdString() + ")");
    if (!condition && hasLastClick_)
        OS_LOG(Info, App) << "       last click " << describeClick().toStdString();
}

// Where the last click went: the UI item under it and what the 3D view
// picks there, so a failure on another machine (e.g. the CI Mac's small
// window, TD-35) can be diagnosed from the log alone.
// The topmost visible item under a window point that takes mouse buttons
// (where a click there goes), or null.
QQuickItem* AcceptanceRunner::itemAt(QPointF p) const
{
    std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* item) -> QQuickItem* {
        // Paint order: by z, then declaration order (the last is on top).
        QList<QQuickItem*> children = item->childItems();
        std::stable_sort(children.begin(), children.end(), [](const QQuickItem* a, const QQuickItem* b) { return a->z() < b->z(); });
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            QQuickItem* child = *it;
            if (!child->isVisible() || child->opacity() <= 0.0)
                continue;
            if (!child->clip() || child->contains(child->mapFromScene(p)))
                if (QQuickItem* hit = find(child))
                    return hit;
        }
        return item->acceptedMouseButtons() != Qt::NoButton && item->contains(item->mapFromScene(p)) ? item : nullptr;
    };
    return find(window_->contentItem());
}

QPointF AcceptanceRunner::uncoveredScreenPoint(const std::vector<Vec3>& candidates) const
{
    for (const Vec3& c : candidates) {
        const QPointF p = screenPoint(c.x, c.y, c.z);
        const bool inside = p.x() >= 0 && p.y() >= 0 && p.x() < window_->width() && p.y() < window_->height();
        const QQuickItem* top = inside ? itemAt(p) : nullptr;
        if (top && top->objectName() == QLatin1String("viewport"))
            return p;
    }
    return candidates.empty() ? QPointF() : screenPoint(candidates.front().x, candidates.front().y, candidates.front().z);
}

QString AcceptanceRunner::describeClick() const
{
    QString path;
    for (QQuickItem* item = itemAt(lastClick_); item && item != window_->contentItem(); item = item->parentItem()) {
        const QString name = item->objectName().isEmpty() ? QString::fromLatin1(item->metaObject()->className()) : item->objectName();
        path = path.isEmpty() ? name : name + QLatin1Char('>') + path;
    }
    const auto& hover = app_->interaction().hover();
    // One name per sel::PickKind (None, Face, Edge, Profile, OriginAxis, Datum).
    const char* const kinds[] = {"nothing", "a face", "an edge", "a sketch profile", "an origin axis",
                                 "a construction axis or plane"};
    constexpr int kindCount = int(sizeof(kinds) / sizeof(kinds[0]));
    static_assert(int(sel::PickKind::Datum) == kindCount - 1, "a name for every PickKind");
    const int kind = int(hover.kind);
    const char* kindName = kind >= 0 && kind < kindCount ? kinds[kind] : "something";
    return QStringLiteral("at %1,%2 in a %3x%4 window: %5; the view picks %6 there")
        .arg(lastClick_.x(), 0, 'f', 0)
        .arg(lastClick_.y(), 0, 'f', 0)
        .arg(window_->width())
        .arg(window_->height())
        .arg(path.isEmpty() ? QStringLiteral("no item") : path)
        .arg(QString::fromLatin1(kindName) + (hover.hit() ? QStringLiteral(" #%1").arg(hover.index) : QString()));
}

void AcceptanceRunner::screenshot(const QString& name)
{
    const QString path = outputDir_ + QLatin1Char('/') + name + QStringLiteral(".png");
    const bool ok = window_->grabWindow().save(path);
    OS_LOG(Info, App) << "screenshot " << path.toStdString() << (ok ? "" : " FAILED");
}

// ---- Script -----------------------------------------------------------------------------

std::vector<AcceptanceRunner::Step> AcceptanceRunner::coreScenario()
{
    auto& in = app_->interaction();
    auto topFaceSelected = [this] {
        const auto& sel = app_->interaction().selection();
        if (sel.size() != 1 || sel.items()[0].kind != sel::SelectionKind::Face)
            return false;
        const auto info = geom::faceInfo(app_->document().body(sel.items()[0].bodyId)->shape(), sel.items()[0].index);
        return info && info->normal.z > 0.999;
    };

    return {
        // 1-2. Launch, create a 20 mm cube with the "B" shortcut.
        // Discoverability: the help card opens from "?" and closes with Esc.
        [=, this] {
            check(app_->bodyCount() == 0, "starts with an empty document");
            check(clickItem(QStringLiteral("helpButton")), "help button");
        },
        [=, this] {
            auto* help = findVisualItem(window_->contentItem(), QStringLiteral("helpOverlay"));
            check(help && help->isVisible(), "help card is shown");
            screenshot(QStringLiteral("00_help"));
            key(Qt::Key_Escape);
            check(help && !help->isVisible(), "Esc closes the help card");
            check(clickItem(QStringLiteral("fileMenuButton")), "File menu");
        },
        [=, this] {
            check(clickItem(QStringLiteral("aboutMenuItem")), "About OpenShape in the File menu");
            auto* about = findVisualItem(window_->contentItem(), QStringLiteral("aboutOverlay"));
            check(about && about->isVisible(), "the About card shows the license and source link");
            screenshot(QStringLiteral("00b_about"));
            key(Qt::Key_Escape);
            check(about && !about->isVisible(), "Esc closes the About card");
            auto* triad = findVisualItem(window_->contentItem(), QStringLiteral("axisTriad"));
            check(triad && triad->isVisible() && triad->width() > 0, "the X/Y/Z axis marker is shown");
        },
        [=, this] {
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
            check(app_->operationValueLabel() == QStringLiteral("Height") && app_->operationValueText() == QStringLiteral("20.00 mm"),
                  "the value chip shows the cube's height", app_->operationValueLabel() + QStringLiteral(" ") + app_->operationValueText());
            screenshot(QStringLiteral("03_face_selected"));
        },
        // 9-10. Drag the arrow upward: live preview, document unchanged.
        [=, this, &in] {
            const auto* op = in.operation();
            if (!op) {
                check(false, "the push/pull tool is there to drag (an earlier step failed)");
                throw AbortScenario{};
            }
            const Vec3 anchor = op->anchor();
            const double px = in.camera().pixelSize(anchor);
            const interact::ArrowStyle style;
            const Vec3 grab = anchor + op->manipulator().direction() * ((style.gapPx + style.shaftPx * 0.6) * px);
            const QPointF from = screenPoint(grab.x, grab.y, grab.z);
            drag(from, from + QPointF(0, -90));
            check(in.operation() && in.operation()->value() > 21.0, "dragging the arrow makes it taller",
                  in.operation() ? num(in.operation()->value()) : QStringLiteral("no operation"));
        },
        // (Previews compute on a worker thread; the runner waited for this one.)
        [=, this, &in] {
            check(in.operation() && in.operation()->hasPreview(), "the drag's preview is shown");
            check(std::abs(bodyHeight() - 20.0) < 1e-9, "preview does not modify the document", num(bodyHeight()));
            screenshot(QStringLiteral("04_drag_preview"));
        },
        // 11. Type the exact new height (typing goes straight to the value field).
        [=, this, &in] {
            type(QStringLiteral("35"));
            check(in.operation() && std::abs(in.operation()->value() - 35.0) < 1e-12, "typing 35 sets the height to 35 mm",
                  in.operation() ? num(in.operation()->value()) : QString());
            screenshot(QStringLiteral("05_typed_35"));
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
        // Booleans through the real UI: a box moved into the plate, both picked
        // in the Model panel (Shift adds), then "Subtract Body 2" in the bar.
        [=, this] {
            check(clickItem(QStringLiteral("tool_union")), "Union tool button");
            check(app_->bodyCount() == 1, "Union with nothing selected changes nothing");
            key(Qt::Key_B);
            check(app_->bodyCount() == 2, "B adds a second body beside the plate");
        },
        [=, this] {
            const QString box = QString::fromStdString(body(1).id().toString());
            check(clickItem(QStringLiteral("historyRow_") + box), "Model panel row selects the box");
            check(app_->operationTitle() == QStringLiteral("Move"), "a selected body offers Move", app_->operationTitle());
            type(QStringLiteral("-30"));
            key(Qt::Key_Return);
        },
        [] {},
        [=, this] {
            const auto bb = geom::boundingBox(body(1).shape());
            check(std::abs(bb.min.x - 40.0) < 1e-6, "typing -30 moves the box into the plate", num(bb.min.x));
            const QString plate = QString::fromStdString(body(0).id().toString());
            check(clickItem(QStringLiteral("historyRow_") + plate), "Model panel row selects the plate");
        },
        [=, this] {
            const QString box = QString::fromStdString(body(1).id().toString());
            check(clickItem(QStringLiteral("historyRow_") + box, Qt::ShiftModifier), "Shift-click adds the box");
        },
        [=, this] {
            check(app_->interaction().selection().size() == 2, "two bodies selected",
                  QString::number(app_->interaction().selection().size()));
            check(app_->selectionSummary() == QStringLiteral("Body 1 + Body 2"), "summary names both bodies",
                  app_->selectionSummary());
            screenshot(QStringLiteral("14_two_bodies"));
            check(clickItem(QStringLiteral("barAction_subtract")), "Subtract button is shown and clickable");
        },
        [] {},
        [=, this] {
            // Plate (5 mm, two holes) minus the 20 x 20 overlap, which contains one hole.
            const double plate = (1800.0 - 2 * kPi * 9.0) * 5.0;
            const double overlap = (400.0 - kPi * 9.0) * 5.0;
            check(std::abs(bodyVolume() - (plate - overlap)) < 1e-3, "subtract removes the overlap", num(bodyVolume()));
            check(!body(1).isVisible(), "the tool body is consumed (hidden)");
            screenshot(QStringLiteral("15_subtracted"));
            key(Qt::Key_Z, Qt::ControlModifier);
            check(std::abs(bodyVolume() - plate) < 1e-3 && body(1).isVisible(),
                  "undo restores both bodies", num(bodyVolume()));
        },
        // Align through the real UI: the box's front face onto the plate's top.
        [=, this] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            click(screenPoint(50, 0, 10)); // front face of the box
            check(app_->interaction().selection().size() == 1
                      && app_->interaction().selection().items()[0].kind == sel::SelectionKind::Face,
                  "the box's front face is selected");
            check(clickItem(QStringLiteral("tool_align")), "Align tool button");
            check(app_->operationTitle() == QStringLiteral("Align") && !app_->operationPrompt().isEmpty(),
                  "Align asks for its target", app_->operationPrompt());
        },
        [=, this] {
            click(screenPoint(20, 15, 5)); // top face of the plate
            const auto* align = dynamic_cast<const interact::AlignOperation*>(app_->interaction().operation());
            check(align && align->hasTarget() && align->canCommit(), "clicking the plate's top face sets the target");
            screenshot(QStringLiteral("16_align_preview"));
            key(Qt::Key_Return);
        },
        [] {},
        [=, this] {
            const auto bb = geom::boundingBox(body(1).shape());
            check(std::abs(bb.min.z - 5.0) < 1e-6, "Enter: the box rests on the plate", num(bb.min.z));
            check(std::abs(bb.center().x - 30.0) < 1e-6 && std::abs(bb.center().y - 15.0) < 1e-6,
                  "centered on the plate's top face", num(bb.center().x) + "," + num(bb.center().y));
            screenshot(QStringLiteral("17_aligned"));
            key(Qt::Key_Z, Qt::ControlModifier);
            check(std::abs(geom::boundingBox(body(1).shape()).min.z) < 1e-6,
                  "undo puts the box back");
        },
        // Rotate through the real UI: pick the box in the Model panel, Rotate,
        // drag the Z ring by 45 degrees.
        [=, this] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            const QString box = QString::fromStdString(body(1).id().toString());
            check(clickItem(QStringLiteral("historyRow_") + box), "Model panel row selects the box again");
        },
        [=, this] {
            check(clickItem(QStringLiteral("tool_rotate")), "Rotate tool button");
            const auto* op = app_->interaction().operation();
            check(op && op->title() == "Rotate" && op->ringCount() == 3, "Rotate shows three rings");
            if (!op || op->ringCount() != 3)
                return;
            const interact::RingManipulator ring = op->ring(2);
            const Camera& cam = app_->interaction().camera();
            auto at = [&](double a) {
                const Vec2 p = cam.project(ring.pointAt(cam, a));
                return QPointF(p.x, p.y);
            };
            const double a0 = kPi / 4;
            mouseMove(at(a0));
            mousePress(at(a0));
            for (int i = 1; i <= 12; ++i)
                mouseMove(at(a0 + kPi / 4 * i / 12.0), Qt::LeftButton);
            mouseRelease(at(a0 + kPi / 4));
            check(app_->interaction().operation() && std::abs(app_->interaction().operation()->value() - 45.0) < 1e-9,
                  "dragging the Z ring turns 45 degrees (15 degree snaps)",
                  app_->interaction().operation() ? num(app_->interaction().operation()->value()) : QStringLiteral("none"));
            screenshot(QStringLiteral("18_rotate_preview"));
            key(Qt::Key_Return);
        },
        [] {},
        [=, this] {
            const auto bb = geom::boundingBox(body(1).shape());
            check(std::abs(bb.size().x - 20.0 * std::sqrt(2.0)) < 1e-6, "Enter: the box is turned 45 degrees",
                  num(bb.size().x));
            key(Qt::Key_Z, Qt::ControlModifier);
            check(std::abs(geom::boundingBox(body(1).shape()).size().x - 20.0) < 1e-6,
                  "undo turns it back");
        },
        // Pattern and Mirror through the real UI (box still selected).
        [=, this] {
            check(clickItem(QStringLiteral("tool_pattern")), "Pattern tool button");
            check(app_->operationTitle() == QStringLiteral("Pattern") && app_->operationCanCommit(),
                  "Pattern previews three copies right away", app_->operationTitle());
            key(Qt::Key_Return);
        },
        [] {},
        [=, this] {
            // 5 mm apart, the copies do not touch the box: separate bodies.
            check(app_->bodyCount() == 4 && std::abs(geom::volume(body(1).shape()) - 8000.0) < 1e-3
                      && std::abs(geom::volume(body(3).shape()) - 8000.0) < 1e-3,
                  "Enter: two copies of the box beside it, as bodies of their own", QString::number(app_->bodyCount()));
            key(Qt::Key_Z, Qt::ControlModifier);
            check(app_->bodyCount() == 2 && std::abs(geom::volume(body(1).shape()) - 8000.0) < 1e-3, "undo: one box again");
            check(clickItem(QStringLiteral("tool_mirror")), "Mirror tool button");
            check(app_->operationTitle() == QStringLiteral("Mirror") && !app_->operationPrompt().isEmpty(),
                  "Mirror asks for a plane", app_->operationPrompt());
        },
        [=, this] {
            click(screenPoint(60, 10, 10)); // the box's +X face
            check(app_->operationCanCommit(), "clicking a flat face sets the mirror plane");
        },
        [=, this] {
            check(clickItem(QStringLiteral("barAction_apply")), "Apply button");
        },
        [] {},
        [=, this] {
            const auto bb = geom::boundingBox(body(1).shape());
            check(std::abs(bb.size().x - 40.0) < 1e-6 && body(1).shape().solidCount() == 1,
                  "mirrored across its face: one 40 mm block", num(bb.size().x));
            screenshot(QStringLiteral("19_mirrored"));
            key(Qt::Key_Z, Qt::ControlModifier);
        },
        // Touch gestures through Qt's touch path: two fingers undo, three redo.
        [=, this] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            key(Qt::Key_B);
            check(app_->bodyCount() == 3, "B adds a third body", QString::number(app_->bodyCount()));
        },
        [=, this] {
            touchTap({QPointF(500, 300), QPointF(620, 320)});
            check(app_->bodyCount() == 2, "a two-finger tap undoes", QString::number(app_->bodyCount()));
        },
        [=, this] {
            touchTap({QPointF(500, 300), QPointF(600, 300), QPointF(700, 300)});
            check(app_->bodyCount() == 3, "a three-finger tap redoes", QString::number(app_->bodyCount()));
            check(app_->touchMode(), "touch switches to the touch layout");
        },
        // The touch layout offers the Pen switch.
        [=, this] {
            check(clickItem(QStringLiteral("penModeButton")), "Pen switch in the touch layout");
            check(app_->penMode(), "Pen mode on");
        },
        [=, this] {
            check(clickItem(QStringLiteral("penModeButton")), "Pen switch again");
            check(!app_->penMode(), "Pen mode off");
        },

        // ================= Sketch tools, extrude options, edges that come along, face edits =================
        [=, this] {
            app_->newDocument();
            check(app_->bodyCount() == 0, "new document for the sketch tools");
            // A known 3D view to come back to (earlier sections turned the camera).
            app_->interaction().setStandardView(StandardView::Isometric, false);
            app_->interaction().fitAll(false);
            key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            check(app_->sketchMode(), "K starts another sketch");
        },
        [] {}, [] {}, [] {},
        // A 40 x 20 rectangle and a line through it that sticks out at the top
        // (off-center: the rectangle's width label sits below its middle).
        [=, this] {
            click(screenPoint(0, 0, 0));
            check(!app_->touchMode(), "a mouse click in the view returns to the mouse layout");
            mouseMove(screenPoint(30, 12, 0));
            type(QStringLiteral("40"));
            key(Qt::Key_Tab);
            type(QStringLiteral("20"));
            key(Qt::Key_Return);
            key(Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
            click(screenPoint(25, -5, 0));
            click(screenPoint(25, 26, 0));
            key(Qt::Key_Escape);
            const auto* s = app_->interaction().sketchSession();
            check(s && s->sketch().lines().size() == 5, "rectangle and a crossing line",
                  s ? QString::number(s->sketch().lines().size()) : QString());
        },
        // Trim cuts the part above the rectangle away.
        [=, this] {
            check(clickItem(QStringLiteral("tool_trim")), "Trim tool button");
            mouseMove(screenPoint(25, 24, 0));
            click(screenPoint(25, 24, 0));
            const auto& sk = app_->interaction().sketchSession()->sketch();
            double topOfLine = 0;
            for (const auto& [id, l] : sk.lines()) {
                const Vec2 a = sk.point(l.start)->position, b = sk.point(l.end)->position;
                if (std::abs(a.x - 25) < 1e-6 && std::abs(b.x - 25) < 1e-6)
                    topOfLine = std::max(a.y, b.y);
            }
            check(std::abs(topOfLine - 20) < 1e-6, "trim stops the line at the rectangle's edge", num(topOfLine));
        },
        // A slot below the rectangle: two centers and a typed width.
        [=, this] {
            check(clickItem(QStringLiteral("tool_slot")), "Slot tool button");
            click(screenPoint(8, -14, 0));
            click(screenPoint(32, -14, 0));
            mouseMove(screenPoint(20, -11, 0));
            type(QStringLiteral("6"));
            key(Qt::Key_Return);
            const auto& sk = app_->interaction().sketchSession()->sketch();
            bool round = sk.arcs().size() == 2;
            for (const auto& [id, a] : sk.arcs())
                round = round && std::abs(sk.arcRadius(id) - 3) < 1e-6;
            check(round, "slot: two 3 mm end arcs", QString::number(sk.arcs().size()));
        },
        // Round the rectangle's top-right corner.
        [=, this] {
            key(Qt::Key_S, Qt::NoModifier, QStringLiteral("s"));
            click(screenPoint(40, 20, 0));
            check(clickItem(QStringLiteral("sketchAction_fillet")), "Fillet on a selected corner");
            check(app_->interaction().sketchSession()->sketch().arcs().size() == 3, "the corner became an arc");
        },
        // Offset the slot 2 mm outward (clicked where its curves really are:
        // the centers snapped to the grid).
        [=, this] {
            const auto* s = app_->interaction().sketchSession();
            std::vector<Vec2> centers;
            double radius = 0;
            for (const auto& [id, a] : s->sketch().arcs())
                if (s->sketch().arcRadius(id) < 3.5) {
                    centers.push_back(s->sketch().point(a.center)->position);
                    radius = s->sketch().arcRadius(id);
                }
            if (centers.size() != 2) {
                check(false, "the slot's centers found");
                return;
            }
            std::sort(centers.begin(), centers.end(), [](Vec2 p, Vec2 q) { return p.x < q.x; });
            const Vec2 mid = (centers[0] + centers[1]) * 0.5;
            click(screenPoint(mid.x, mid.y + radius, 0));
            click(screenPoint(mid.x, mid.y - radius, 0), Qt::ShiftModifier);
            click(screenPoint(centers[0].x - radius, centers[0].y, 0), Qt::ShiftModifier);
            click(screenPoint(centers[1].x + radius, centers[1].y, 0), Qt::ShiftModifier);
            check(s->selection().size() == 4, "the slot's four curves selected", QString::number(s->selection().size()));
            check(clickItem(QStringLiteral("sketchAction_offset")), "Offset on the selected curves");
            mouseMove(screenPoint(mid.x, mid.y - radius - 3, 0));
            type(QStringLiteral("2"));
            key(Qt::Key_Return);
            bool offset = false;
            for (const auto& [id, a] : s->sketch().arcs())
                offset = offset || std::abs(s->sketch().arcRadius(id) - 5) < 1e-6;
            check(offset && s->sketch().arcs().size() == 5, "offset slot with 5 mm end arcs",
                  QString::number(s->sketch().arcs().size()));
            screenshot(QStringLiteral("20_sketch_tools"));
        },
        [=, this] { check(clickItem(QStringLiteral("finishSketchButton")), "Finish sketch button (tools)"); },
        [] {}, [] {}, [] {},
        // Symmetric extrusion of the rectangle's left part: 10 mm, 5 each side.
        [=, this] {
            click(screenPoint(8, 8, 0));
            check(app_->operationTitle() == QStringLiteral("Extrude"), "left part selected for extrusion", app_->operationTitle());
            check(clickItem(QStringLiteral("action_symmetric")), "Symmetric option");
            check(app_->operationValueLabel() == QStringLiteral("Thickness"), "symmetric extrusion shows the thickness",
                  app_->operationValueLabel());
            type(QStringLiteral("10"));
            key(Qt::Key_Return);
            const auto bb = geom::boundingBox(app_->document().bodies().front()->shape());
            check(app_->bodyCount() == 1 && std::abs(bb.min.z + 5) < 1e-6 && std::abs(bb.max.z - 5) < 1e-6,
                  "symmetric: 5 mm on each side of the sketch", num(bb.min.z) + QStringLiteral("..") + num(bb.max.z));
        },
        [] {},
        // Up to face: the right part ends on the left block's top.
        [=, this, &in] {
            // (38, 3): in the iso view (30, 8, 0) lands exactly on the block's bottom edge.
            click(screenPoint(38, 3, 0));
            const auto& picked = in.selection();
            check(app_->operationTitle() == QStringLiteral("Extrude"), "the right part selected for extrusion",
                  app_->operationTitle() + QStringLiteral(" / ")
                      + QString::number(picked.empty() ? -1 : int(picked.items().front().kind)));
            check(clickItem(QStringLiteral("action_upToFace")), "Up to face option");
            click(screenPoint(10, 10, 5));
            const auto* op = app_->interaction().operation();
            check(op && std::abs(op->value() - 5) < 1e-6, "the clicked top sets the distance", op ? num(op->value()) : QString());
            key(Qt::Key_Return);
            check(app_->bodyCount() == 2
                      && std::abs(geom::boundingBox(body(1).shape()).max.z - 5) < 1e-6,
                  "up to face: the new block ends at z = 5");
        },
        // Round the left block's front top edge, then make the block taller: the rounding comes along.
        [=, this, &in] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            click(screenPoint(10, 0, 5));
            check(in.selection().size() == 1 && in.selection().items()[0].kind == sel::SelectionKind::Edge,
                  "clicking the block's top edge selects it");
            type(QStringLiteral("2"));
            key(Qt::Key_Return);
        },
        [=, this] {
            const geom::Shape before = app_->document().bodies().front()->shape();
            click(screenPoint(10, 12, 5));
            check(app_->operationValueLabel() == QStringLiteral("Height") && app_->operationValueText() == QStringLiteral("10.00 mm"),
                  "the block's top shows its height", app_->operationValueLabel() + QStringLiteral(" ") + app_->operationValueText());
            type(QStringLiteral("16"));
            key(Qt::Key_Return);
            const geom::Shape after = app_->document().bodies().front()->shape();
            check(std::abs(geom::boundingBox(after).max.z - 11) < 1e-6, "the block is now 16 mm tall",
                  num(geom::boundingBox(after).max.z));
            check(after.faceCount() == before.faceCount(), "the rounded edge moved up with the face (no step)",
                  QString::number(after.faceCount()) + QStringLiteral(" vs ") + QString::number(before.faceCount()));
            screenshot(QStringLiteral("21_edge_comes_along"));
        },
        // A 4 mm hole through the right block.
        [=, this] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            click(screenPoint(30, 12, 5));
            key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            check(app_->sketchMode(), "K on the right block's top face");
        },
        [] {}, [] {}, [] {},
        [=, this] {
            key(Qt::Key_C, Qt::NoModifier, QStringLiteral("c"));
            click(screenPoint(30, 10, 5));
            mouseMove(screenPoint(31, 10, 5));
            type(QStringLiteral("4"));
            key(Qt::Key_Return);
            check(clickItem(QStringLiteral("finishSketchButton")), "Finish sketch button (hole)");
        },
        [] {}, [] {}, [] {},
        [=, this] {
            if (app_->bodyCount() < 2)
                return; // reported by the up-to-face checks
            holeBlockVolume_ = geom::volume(body(1).shape());
            click(screenPoint(30, 10, 5));
            type(QStringLiteral("-5"));
        },
        [=, this] {
            if (app_->bodyCount() < 2) {
                check(false, "a second block for the hole");
                return;
            }
            check(clickItem(QStringLiteral("action_throughAll")), "through-all for the hole");
            key(Qt::Key_Return);
            const double v = geom::volume(body(1).shape());
            check(std::abs(holeBlockVolume_ - v - kPi * 4 * 5) < 1e-3, "a 4 mm hole through the block", num(v));
        },
        // Click the hole's wall: it offers its diameter; type a new one. (Zoomed
        // in, and 2 mm down the wall, so the rim edge is not what gets picked.)
        [=, this] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            app_->interaction().fitAll(false);
            // In small windows the fitted view is too far out: a click on the
            // wall would pick the rim edge. Zoom in on the hole (TD-35).
            const QPointF hole = screenPoint(30, 10, 3);
            app_->interaction().wheel({hole.x(), hole.y()}, 4);
        },
        [=, this] {
            click(screenPoint(30 - 2 * 0.7071, 10 + 2 * 0.7071, 3));
            check(app_->operationTitle() == QStringLiteral("Offset") && app_->operationValueLabel() == QStringLiteral("Diameter"),
                  "the hole wall offers its diameter", app_->operationTitle() + QStringLiteral(" ") + app_->operationValueLabel());
            type(QStringLiteral("5"));
            key(Qt::Key_Return);
            if (app_->bodyCount() < 2)
                return;
            const double v = geom::volume(body(1).shape());
            check(std::abs(holeBlockVolume_ - v - kPi * 6.25 * 5) < 1e-3, "the hole is now 5 mm", num(v));
        },
        // Select the wall again and press Delete: the hole is gone.
        [=, this] {
            key(Qt::Key_Escape);
            key(Qt::Key_Escape);
            click(screenPoint(30 - 2.5 * 0.7071, 10 + 2.5 * 0.7071, 3));
            key(Qt::Key_Delete);
            if (app_->bodyCount() < 2)
                return;
            const double v = geom::volume(body(1).shape());
            check(std::abs(v - holeBlockVolume_) < 1e-3, "Delete removes the hole", num(v));
            screenshot(QStringLiteral("22_face_edits"));
        },
    };
}

// ---- Scenarios ----------------------------------------------------------------------

namespace {
std::vector<AcceptanceScenario>& registry()
{
    static std::vector<AcceptanceScenario> scenarios;
    return scenarios;
}
} // namespace

bool registerAcceptanceScenario(AcceptanceScenario scenario)
{
    registry().push_back(std::move(scenario));
    return true;
}

std::vector<AcceptanceScenario> acceptanceScenarios()
{
    std::vector<AcceptanceScenario> list = registry();
    std::stable_sort(list.begin(), list.end(), [](const AcceptanceScenario& a, const AcceptanceScenario& b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return list;
}

QQuickItem* AcceptanceRunner::findItem(const QString& objectName) const
{
    auto* item = window_->findChild<QQuickItem*>(objectName);
    if (!item)
        item = findVisualItem(window_->contentItem(), objectName);
    // Two items may share a name (one per layout, or a delegate being
    // replaced): prefer the one on screen.
    if (item && !item->isVisible())
        if (QQuickItem* shown = findVisibleVisualItem(window_->contentItem(), objectName))
            return shown;
    return item;
}

void AcceptanceRunner::resizeWindow(int width, int height)
{
    // Phone-sized windows are below the desktop minimum (Main.qml).
    window_->setMinimumSize(QSize(std::min(width, initialMinimum_.width()), std::min(height, initialMinimum_.height())));
    window_->resize(width, height);
    QCoreApplication::processEvents();
    OS_LOG(Info, App) << "acceptance: window resized to " << width << "x" << height << " (it is " << window_->width() << "x"
                      << window_->height() << ")";
}

void AcceptanceRunner::start()
{
    initialSize_ = window_->size();
    initialMinimum_ = window_->minimumSize();
    startedInPerspective_ = app_->perspective();
    initialAppFolder_ = app_->appFolder();
    std::vector<AcceptanceScenario> scenarios = acceptanceScenarios();
    scenarios.insert(scenarios.begin(), AcceptanceScenario{QStringLiteral("core"), 0, [](AcceptanceRunner& r) {
                                                              return r.coreScenario();
                                                          }});
    QStringList unknown = filter_;
    bool first = true;
    for (const AcceptanceScenario& scenario : scenarios) {
        unknown.removeAll(scenario.name);
        if (!filter_.isEmpty() && !filter_.contains(scenario.name))
            continue;
        scenarioStarts_.push_back(steps_.size());
        const bool reset = !first;
        steps_.push_back([this, name = scenario.name, reset] { beginScenario(name, reset); });
        if (reset)
            steps_.insert(steps_.end(), {[] {}, [] {}, [] {}}); // the view animation after the reset
        for (Step& step : scenario.steps(*this))
            steps_.push_back(std::move(step));
        first = false;
    }
    for (const QString& name : unknown)
        check(false, QStringLiteral("scenario '%1' exists").arg(name));
    QTimer::singleShot(400, this, &AcceptanceRunner::runNext);
}

void AcceptanceRunner::beginScenario(const QString& name, bool reset)
{
    endScenario();
    scenario_ = name;
    OS_LOG(Info, App) << "acceptance: scenario '" << name.toStdString() << "'";
    if (!reset)
        return;
    // A clean slate: no sketch, operation or overlay; a new document in
    // millimeters, mouse layout, isometric view.
    if (app_->sketchMode())
        app_->finishSketch();
    app_->cancelOperation();
    for (const char* overlay : {"helpOverlay", "aboutOverlay", "preferencesOverlay", "unsavedDialog", "saveNamePrompt"})
        if (QQuickItem* item = findItem(QString::fromLatin1(overlay)))
            item->setVisible(false);
    if (!app_->recoveryItems().isEmpty())
        app_->postponeRecovery();
    // Default preferences (a scenario may have changed them).
    app_->setDefaultUnit(QStringLiteral("mm"));
    app_->setSketchGridSnap(true);
    app_->setRecoveryInterval(60);
    app_->setHoleAllowance(doc::kDefaultHoleAllowance);
    app_->setPenMode(false);
    app_->setTouchMode(false);
    // The window a scenario may have made phone-sized (Compact) comes back,
    // without a simulated safe area or an open compact panel.
    window_->setProperty("simulatedSafeArea", QVariant());
    app_->setAppFolder(initialAppFolder_); // a scenario may save as on an iPhone
    window_->setProperty("historyOpen", false);
    window_->setProperty("viewMenuOpen", false);
    if (window_->size() != initialSize_) {
        window_->resize(initialSize_);
        window_->setMinimumSize(initialMinimum_);
    }
    app_->setPerspective(true); // the default (a scenario may have switched)
    app_->newDocument();
    app_->setDisplayUnit(QStringLiteral("mm"));
    app_->setView(QStringLiteral("iso"));
    mouseMove({window_->width() / 2.0, window_->height() / 2.0});
    if (QQuickItem* viewport = findItem(QStringLiteral("viewport")))
        viewport->forceActiveFocus();
}

void AcceptanceRunner::endScenario()
{
    if (scenario_.isEmpty())
        return;
    summary_ << QStringLiteral("%1: %2/%3").arg(scenario_).arg(scenarioChecks_ - scenarioFailures_).arg(scenarioChecks_);
    scenarioChecks_ = 0;
    scenarioFailures_ = 0;
}

void AcceptanceRunner::runNext()
{
    // A slow machine (the CI Mac) may still be animating the view after the
    // fixed step delay; clicks computed from a moving camera miss. Wait for
    // the animation to end (up to 3 s) before the next step (TD-31, TD-35).
    // Previews compute on a worker thread: wait until the last one is shown
    // (up to 30 s), so a step sees the preview (or the error) of what the
    // step before it typed or dragged. The event loop runs meanwhile: the
    // result arrives as the app would get it.
    const bool animating = app_->interaction().isAnimating() && animationWaitMs_ < 3000;
    const bool previewing = app_->interaction().previewBusy() && previewWaitMs_ < 30000;
    if (animating || previewing) {
        (animating ? animationWaitMs_ : previewWaitMs_) += 10;
        QTimer::singleShot(10, this, &AcceptanceRunner::runNext);
        return;
    }
    if (previewWaitMs_ >= 30000)
        check(false, QStringLiteral("previews finish within 30 s"));
    animationWaitMs_ = 0;
    previewWaitMs_ = 0;
    if (next_ < steps_.size()) {
        try {
            steps_[next_++]();
        } catch (const AbortScenario&) {
            const auto nextStart = std::upper_bound(scenarioStarts_.begin(), scenarioStarts_.end(), next_ - 1);
            next_ = nextStart == scenarioStarts_.end() ? steps_.size() : *nextStart;
        }
        QTimer::singleShot(kStepDelayMs, this, &AcceptanceRunner::runNext);
        return;
    }
    endScenario();
    OS_LOG(Info, App) << "acceptance: scenarios " << summary_.join(QStringLiteral(", ")).toStdString();
    OS_LOG(Info, App) << "acceptance: " << (checks_ - failures_) << "/" << checks_ << " checks passed";
    QCoreApplication::exit(failures_);
}

} // namespace os::app
