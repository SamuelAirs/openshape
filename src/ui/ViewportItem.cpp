// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/ViewportItem.h"

#include "render/ViewportRenderer.h"
#include "ui/AppController.h"

#include <QtGui/QPointingDevice>

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

void ViewportItem::mousePressEvent(QMouseEvent* event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    // A real mouse (not a touch or pen turned into mouse events) leaves touch mode.
    if (controller_ && event->source() == Qt::MouseEventNotSynthesized && event->pointingDevice()
        && event->pointingDevice()->type() == QInputDevice::DeviceType::Mouse)
        controller_->setTouchMode(false);
    if (controller_)
        controller_->interaction().pointerPress(toPointer(event, event->button()));
    event->accept();
}

void ViewportItem::mouseMoveEvent(QMouseEvent* event)
{
    if (controller_) {
        const Qt::MouseButton held = event->buttons().testFlag(Qt::LeftButton) ? Qt::LeftButton
            : event->buttons().testFlag(Qt::RightButton)                      ? Qt::RightButton
            : event->buttons().testFlag(Qt::MiddleButton)                     ? Qt::MiddleButton
                                                                              : Qt::NoButton;
        controller_->interaction().pointerMove(toPointer(event, held));
    }
    event->accept();
}

void ViewportItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (controller_)
        controller_->interaction().pointerRelease(toPointer(event, event->button()));
    event->accept();
}

void ViewportItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (controller_ && event->button() == Qt::LeftButton)
        controller_->interaction().pointerDoubleClick(toPointer(event, event->button()));
    event->accept();
}

void ViewportItem::hoverMoveEvent(QHoverEvent* event)
{
    if (controller_)
        controller_->interaction().pointerMove(toPointer(event, Qt::NoButton));
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
    auto& interaction = controller_->interaction();
    if (event->type() == QEvent::TouchCancel) {
        interaction.cancelPointer();
        gestures_ = {};
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
    using Kind = interact::TouchIntent::Kind;
    for (const auto& intent : gestures_.update(points, double(event->timestamp()) / 1000.0)) {
        interact::PointerEvent e;
        e.device = interact::PointerDevice::Touch;
        e.button = interact::PointerButton::Left;
        e.position = intent.position;
        switch (intent.kind) {
        case Kind::PointerPress:
            forceActiveFocus(Qt::MouseFocusReason);
            interaction.pointerPress(e);
            break;
        case Kind::PointerMove: interaction.pointerMove(e); break;
        case Kind::PointerRelease: interaction.pointerRelease(e); break;
        case Kind::PointerCancel: interaction.cancelPointer(); break;
        case Kind::DoubleTap: interaction.pointerDoubleClick(e); break;
        case Kind::Pan: interaction.twoFingerPan(intent.from, intent.to); break;
        case Kind::Pinch: interaction.pinch(intent.position, intent.scale); break;
        case Kind::Undo: controller_->undoWithFeedback(); break;
        case Kind::Redo: controller_->redoWithFeedback(); break;
        }
    }
    event->accept();
}

} // namespace os::ui
