// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QPointer>
#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

#include "ui/MeasureController.h"

namespace os::ui {

// The scanned mesh's preview in the Measure workspace (MeasureScan.qml): its
// triangles flat-shaded, seen from `elevation` degrees above and turned by
// `angle` about the vertical (a turntable when QML animates the angle),
// drawn back to front with QPainter. Cheap: a few thousand triangles.
class ScanPreview : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(os::ui::MeasureController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(double angle READ angle WRITE setAngle NOTIFY angleChanged)
    Q_PROPERTY(double elevation READ elevation WRITE setElevation NOTIFY angleChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit ScanPreview(QQuickItem* parent = nullptr);

    MeasureController* controller() const { return controller_; }
    void setController(MeasureController* controller);
    double angle() const { return angle_; }
    void setAngle(double degrees);
    double elevation() const { return elevation_; }
    void setElevation(double degrees);
    QColor color() const { return color_; }
    void setColor(const QColor& color);

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();
    void angleChanged();
    void colorChanged();

private:
    QPointer<MeasureController> controller_;
    QMetaObject::Connection connection_;
    double angle_ = 30;
    double elevation_ = 28;
    QColor color_{0x29, 0x77, 0xED};
};

} // namespace os::ui
