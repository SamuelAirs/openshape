// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "interaction/PalmRejection.h"
#include "interaction/TouchGestures.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickRhiItem>

#include <optional>
#include <vector>

namespace os::ui {

class AppController;

// The 3D viewport. Translates Qt input (mouse, touch, stylus, wheel) into
// application-level pointer events for the InteractionController and hosts
// the QRhi renderer.
class ViewportItem : public QQuickRhiItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(Viewport)
    Q_PROPERTY(os::ui::AppController* controller READ controller WRITE setController NOTIFY controllerChanged)

public:
    explicit ViewportItem(QQuickItem* parent = nullptr);

    AppController* controller() const { return controller_; }
    void setController(AppController* controller);

signals:
    void controllerChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void touchEvent(QTouchEvent* event) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onViewChanged();
    // Takes the keyboard focus for a press. True when the press only closed
    // the sketch's dimension editor (AppController::swallowViewPress): it
    // goes no further then, nor do its moves and its release (swallowing_).
    bool focusForPress();
    bool swallowing_ = false;

    QPointer<AppController> controller_;
    QTimer animationTimer_;

    // Taps, drags, two-finger pan/pinch, two/three-finger undo/redo.
    interact::TouchGestureRecognizer gestures_;
    void dispatchTouch(const std::vector<interact::TouchIntent>& intents);

    // The pen (Apple Pencil): its contact, seen on the window before Qt
    // Quick turns it into mouse events, drives palm rejection; its latest
    // hover position tells its hover from the mouse's (hoverMoveEvent).
    static double seconds(); // a monotonic clock for PalmRejection
    void penDown();
    void penUp();
    interact::PalmRejection palm_;
    std::optional<QPointF> penHoverAt_;
    QPointer<QQuickWindow> watchedWindow_;
};

} // namespace os::ui
