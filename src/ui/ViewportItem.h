#pragma once

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickRhiItem>

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

private:
    void onViewChanged();

    QPointer<AppController> controller_;
    QTimer animationTimer_;

    // Double-tap detection (touch has no native double-click here).
    qint64 lastTapTime_ = 0;
    QPointF lastTapPosition_;

    // Two-finger gesture tracking.
    bool twoFinger_ = false;
    QPointF lastCentroid_;
    double lastSpan_ = 0;
};

} // namespace os::ui
