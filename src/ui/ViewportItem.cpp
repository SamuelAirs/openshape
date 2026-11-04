// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/ViewportItem.h"

#include "render/ViewportRenderer.h"
#include "ui/AppController.h"

#include <QtGui/QPointingDevice>
#include <QtGui/QTabletEvent>
#include <QtQuick/QQuickWindow>

#include <chrono>

namespace os::ui {

namespace {

interact::PointerDevice deviceOf(const QPointerEvent* event)
{
    const QPointingDevice* device = event->pointingDevice();
    if (device && device->pointerType() == QPointingDevice::PointerType::Pen)
        return interact::PointerDevice::Pen;
    if (device && device->type() == QInputDevice::DeviceType::TouchScreen)
        return interact::PointerDevice::Touch;
    return interact::PointerDevice::Mouse;
}

interact::PointerButton buttonOf(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return interact::PointerButton::Left;
    case Qt::MiddleButton: return interact::PointerButton::Middle;
    case Qt::RightButton: return interact::PointerButton::Right;
    default: return interact::PointerButton::None;
    }
}

interact::Modifiers modifiersOf(Qt::KeyboardModifiers m)
{
    return {m.testFlag(Qt::ShiftModifier), m.testFlag(Qt::ControlModifier), m.testFlag(Qt::AltModifier)};
}

bool isPen(const QPointerEvent* event)
{
    return event->pointingDevice() && event->pointingDevice()->pointerType() == QPointingDevice::PointerType::Pen;
}

double millisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

interact::PointerEvent toPointer(const QSinglePointEvent* event, Qt::MouseButton button)
{
    interact::PointerEvent e;
    e.device = deviceOf(event);
    e.button = buttonOf(button);
    e.position = {event->position().x(), event->position().y()};
    e.modifiers = modifiersOf(event->modifiers());
    return e;
}

} // namespace

ViewportItem::ViewportItem(QQuickItem* parent) : QQuickRhiItem(parent)
{
    setSampleCount(4);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
    setFlag(ItemAcceptsInputMethod, false);
    animationTimer_.setInterval(8);
    connect(&animationTimer_, &QTimer::timeout, this, [this] {
        if (!controller_ || !controller_->interaction().advanceAnimation())
            animationTimer_.stop();
        update();
    });
}

double ViewportItem::seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void ViewportItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickRhiItem::itemChange(change, value);
    if (change != ItemSceneChange)
        return;
    if (watchedWindow_)
        watchedWindow_->removeEventFilter(this);
    watchedWindow_ = value.window;
    if (watchedWindow_)
        watchedWindow_->installEventFilter(this);
}

bool ViewportItem::eventFilter(QObject* watched, QEvent* event)
{
    // The pen's own events, before Qt Quick turns the ones no item takes
    // into mouse events (a press, a drag, or a hover without buttons): the
    // pen's contact for palm rejection, and which hover is the pen's (Qt
    // Quick's hover event no longer says what device moved).
    if (watched != watchedWindow_)
        return false;
    switch (event->type()) {
    case QEvent::TabletPress:
        penDown();
        break;
    case QEvent::TabletRelease:
        penUp();
        break;
    case QEvent::TabletMove: {
        const auto* tablet = static_cast<QTabletEvent*>(event);
        if (tablet->buttons() == Qt::NoButton)
            penHoverAt_ = tablet->scenePosition();
        break;
    }
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (isPen(mouse)) {
            if (mouse->buttons() == Qt::NoButton)
                penHoverAt_ = mouse->scenePosition();
        } else if (mouse->source() == Qt::MouseEventNotSynthesized) {
            penHoverAt_.reset(); // a real mouse moved
        }
        break;
    }
    case QEvent::WindowDeactivate:
        penUp(); // the pen's release may never come
        break;
    default:
        break;
    }
    return false;
}

void ViewportItem::penDown()
{
    if (palm_.penIsDown())
        return;
    palm_.penDown(seconds());
    penHoverAt_.reset();
    // Fingers on the view now are the hand: whatever they did ends (a
    // one-finger press is cancelled, a pan or pinch stops).
    dispatchTouch(gestures_.abandon());
}

void ViewportItem::penUp()
{
    if (palm_.penIsDown())
        palm_.penUp(seconds());
}

void ViewportItem::setController(AppController* controller)
{
    if (controller_ == controller)
        return;
    if (controller_)
        disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller_) {
        connect(controller_, &AppController::viewChanged, this, &ViewportItem::onViewChanged);
        controller_->interaction().setViewportSize({width(), height()});
    }
    emit controllerChanged();
    update();
}

void ViewportItem::onViewChanged()
{
    if (controller_ && controller_->interaction().isAnimating() && !animationTimer_.isActive())
        animationTimer_.start();
    update();
}

QQuickRhiItemRenderer* ViewportItem::createRenderer()
{
    QPointer<AppController> controller = controller_;
    return new render::ViewportRenderer([controller] {
        return controller ? controller->interaction().renderScene() : interact::RenderScene{};
    });
}

void ViewportItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickRhiItem::geometryChange(newGeometry, oldGeometry);
    if (controller_)
        controller_->interaction().setViewportSize({newGeometry.width(), newGeometry.height()});
}

bool ViewportItem::focusForPress()
{
    if (controller_)
        controller_->takeSwallowedViewPress(); // (a stale request from focus that went elsewhere)
    forceActiveFocus(Qt::MouseFocusReason);
    // The editor asked while it lost the focus to this press.
    return controller_ && controller_->takeSwallowedViewPress();
}

void ViewportItem::mousePressEvent(QMouseEvent* event)
{
    if (isPen(event))
        penDown(); // (normally seen already, as the tablet press)
    const bool swallow = focusForPress();
    // A real mouse (not a touch or pen turned into mouse events) leaves touch mode.
    if (controller_ && event->source() == Qt::MouseEventNotSynthesized && event->pointingDevice()
        && event->pointingDevice()->type() == QInputDevice::DeviceType::Mouse)
        controller_->setTouchMode(false);
    if (swallow)
        swallowing_ = true;
    else if (controller_ && event->button() == Qt::LeftButton
             && controller_->referenceScalePointer(AppController::ScalePointer::Press, event->position(), false))
        scalePicking_ = true; // Set scale on a photo takes the left button
    else if (controller_)
        controller_->interaction().pointerPress(toPointer(event, event->button()));
    event->accept();
}

void ViewportItem::mouseMoveEvent(QMouseEvent* event)
{
    if (swallowing_ && event->buttons() != Qt::NoButton) {
        event->accept(); // the press that closed the dimension editor, dragged
        return;
    }
    swallowing_ = false;
    if (scalePicking_ && controller_ && event->buttons().testFlag(Qt::LeftButton)) {
        controller_->referenceScalePointer(AppController::ScalePointer::Move, event->position(), false);
        event->accept();
        return;
    }
    if (controller_) {
        const Qt::MouseButton held = event->buttons().testFlag(Qt::LeftButton) ? Qt::LeftButton
            : event->buttons().testFlag(Qt::RightButton)                      ? Qt::RightButton
            : event->buttons().testFlag(Qt::MiddleButton)                     ? Qt::MiddleButton
                                                                              : Qt::NoButton;
        // The GUI thread's whole cost of a move: the controller and the QML
        // bindings its state change re-evaluates (synchronously).
        const auto start = std::chrono::steady_clock::now();
        controller_->interaction().pointerMove(toPointer(event, held));
        controller_->notePointerMove(millisecondsSince(start), held != Qt::NoButton);
    }
    event->accept();
}

void ViewportItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (isPen(event))
        penUp();
    if (swallowing_) {
        swallowing_ = false;
        event->accept();
        return;
    }
    if (scalePicking_ && event->button() == Qt::LeftButton) {
        scalePicking_ = false;
        if (controller_)
            controller_->referenceScalePointer(AppController::ScalePointer::Release, event->position(), false);
        event->accept();
        return;
    }
    if (controller_) {
        controller_->interaction().pointerRelease(toPointer(event, event->button()));
        controller_->notePointerRelease();
    }
    event->accept();
}

void ViewportItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (controller_ && event->button() == Qt::LeftButton && !swallowing_)
        controller_->interaction().pointerDoubleClick(toPointer(event, event->button()));
    event->accept();
}

void ViewportItem::hoverMoveEvent(QHoverEvent* event)
{
    if (!controller_)
        return;
    interact::PointerEvent e = toPointer(event, Qt::NoButton);
    // Qt Quick makes a hover event of the mouse move it made of the pen's
    // hover (Apple Pencil over an iPad), without the pen as its device: the
    // pen's latest hover position says it is the pen (its tolerances, and
    // never the mouse's).
    if (penHoverAt_ && (event->scenePosition() - *penHoverAt_).manhattanLength() < 0.5)
        e.device = interact::PointerDevice::Pen;
    if (controller_->referenceScalePointer(AppController::ScalePointer::Hover, event->position(), false))
        return;
    controller_->interaction().pointerMove(e);
}

void ViewportItem::hoverLeaveEvent(QHoverEvent*)
{
    if (controller_)
        controller_->interaction().pointerLeave();
}

void ViewportItem::wheelEvent(QWheelEvent* event)
{
    if (!controller_)
        return;
    // Precision touchpads send pixelDelta; wheels send angleDelta (120 per notch).
    double steps = event->angleDelta().y() / 120.0;
    if (steps == 0 && !event->pixelDelta().isNull())
        steps = event->pixelDelta().y() / 50.0;
    controller_->interaction().wheel({event->position().x(), event->position().y()}, steps);
    event->accept();
}

void ViewportItem::touchEvent(QTouchEvent* event)
{
    if (!controller_) {
        event->ignore();
        return;
    }
    controller_->setTouchMode(true);
    if (event->type() == QEvent::TouchCancel) {
        controller_->interaction().cancelPointer(interact::PointerDevice::Touch); // never the pen's stroke
        gestures_ = {};
        palm_.touchesCancelled();
        swallowing_ = false;
        event->accept();
        return;
    }
    std::vector<interact::TouchPoint> points;
    for (const QEventPoint& p : event->points()) {
        interact::TouchPoint tp;
        tp.id = p.id();
        tp.position = {p.position().x(), p.position().y()};
        switch (p.state()) {
        case QEventPoint::Pressed: tp.state = interact::TouchPoint::State::Pressed; break;
        case QEventPoint::Released: tp.state = interact::TouchPoint::State::Released; break;
        case QEventPoint::Stationary: tp.state = interact::TouchPoint::State::Stationary; break;
        default: tp.state = interact::TouchPoint::State::Moved; break;
        }
        points.push_back(tp);
    }
    // The hand holding the pen is not a finger (PalmRejection).
    points = palm_.filter(points, seconds());
    dispatchTouch(gestures_.update(points, double(event->timestamp()) / 1000.0));
    event->accept();
}

void ViewportItem::dispatchTouch(const std::vector<interact::TouchIntent>& intents)
{
    if (!controller_)
        return;
    auto& interaction = controller_->interaction();
    using Kind = interact::TouchIntent::Kind;
    for (const auto& intent : intents) {
        interact::PointerEvent e;
        e.device = interact::PointerDevice::Touch;
        e.button = interact::PointerButton::Left;
        e.position = intent.position;
        switch (intent.kind) {
        case Kind::PointerPress:
            // A finger that closes the dimension editor only closes it.
            if (focusForPress())
                swallowing_ = true;
            else if (controller_->referenceScalePointer(AppController::ScalePointer::Press, {e.position.x, e.position.y}, true))
                scalePicking_ = true; // Set scale on a photo takes the finger
            else
                interaction.pointerPress(e);
            break;
        case Kind::PointerMove: {
            if (swallowing_)
                break;
            if (scalePicking_) {
                controller_->referenceScalePointer(AppController::ScalePointer::Move, {e.position.x, e.position.y}, true);
                break;
            }
            const auto start = std::chrono::steady_clock::now();
            interaction.pointerMove(e);
            controller_->notePointerMove(millisecondsSince(start), true);
            break;
        }
        case Kind::PointerRelease:
            if (swallowing_) {
                swallowing_ = false;
                break;
            }
            if (scalePicking_) {
                scalePicking_ = false;
                controller_->referenceScalePointer(AppController::ScalePointer::Release, {e.position.x, e.position.y}, true);
                break;
            }
            interaction.pointerRelease(e);
            controller_->notePointerRelease();
            break;
        case Kind::PointerCancel:
            swallowing_ = false;
            if (scalePicking_) {
                scalePicking_ = false;
                controller_->referenceScalePointer(AppController::ScalePointer::Cancel, {}, true);
            }
            interaction.cancelPointer(interact::PointerDevice::Touch);
            break;
        case Kind::DoubleTap:
            if (!swallowing_)
                interaction.pointerDoubleClick(e);
            break;
        case Kind::Pan: interaction.twoFingerPan(intent.from, intent.to); break;
        case Kind::Pinch: interaction.pinch(intent.position, intent.scale); break;
        case Kind::Undo: controller_->undoWithFeedback(); break;
        case Kind::Redo: controller_->redoWithFeedback(); break;
        }
    }
}

} // namespace os::ui
