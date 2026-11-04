// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/ScanPreview.h"

#include "ui/MeasureController.h"

#include <QtGui/QPainter>
#include <QtGui/QPolygonF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace os::ui {

ScanPreview::ScanPreview(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void ScanPreview::setController(MeasureController* controller)
{
    if (controller == controller_)
        return;
    disconnect(connection_);
    controller_ = controller;
    if (controller)
        connection_ = connect(controller, &MeasureController::scanChanged, this, [this] { update(); });
    emit controllerChanged();
    update();
}

void ScanPreview::setAngle(double degrees)
{
    if (degrees == angle_)
        return;
    angle_ = degrees;
    emit angleChanged();
    update();
}

void ScanPreview::setElevation(double degrees)
{
    if (degrees == elevation_)
        return;
    elevation_ = degrees;
    emit angleChanged();
    update();
}

void ScanPreview::setColor(const QColor& color)
{
    if (color == color_)
        return;
    color_ = color;
    emit colorChanged();
    update();
}

void ScanPreview::paint(QPainter* painter)
{
    const measure::ScanMesh* mesh = controller_ ? controller_->scanMesh() : nullptr;
    if (!mesh || mesh->triangles.empty() || width() <= 0 || height() <= 0)
        return;
    // Centered on its bounding box; turned about z, then tilted toward the viewer.
    Vec3 lo = mesh->vertices.front(), hi = lo;
    for (const Vec3& v : mesh->vertices) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    const Vec3 c = (lo + hi) * 0.5;
    const double a = angle_ * kPi / 180, e = elevation_ * kPi / 180;
    const double ca = std::cos(a), sa = std::sin(a), ce = std::cos(e), se = std::sin(e);
    // View coordinates: x right, y up, z toward the viewer.
    std::vector<Vec3> view(mesh->vertices.size());
    double extent = 1e-9;
    for (std::size_t i = 0; i < view.size(); ++i) {
        const Vec3 p = mesh->vertices[i] - c;
        const double x = ca * p.x - sa * p.y;
        const double yIn = sa * p.x + ca * p.y; // away from the viewer before the tilt
        // Seen from above: far points rise, high points come nearer.
        const Vec3 v{x, ce * p.z + se * yIn, ce * yIn - se * p.z}; // z: away from the viewer
        view[i] = {v.x, v.y, -v.z};
        extent = std::max({extent, std::abs(v.x), std::abs(v.y)});
    }
    const double scale = 0.42 * std::min(width(), height()) / extent;
    const QPointF center(width() / 2, height() / 2);
    struct Face {
        double depth;
        double shade;
        QPolygonF polygon;
    };
    std::vector<Face> faces;
    faces.reserve(mesh->triangles.size());
    const Vec3 light = Vec3{-0.35, 0.55, 0.75}.normalized();
    for (const auto& t : mesh->triangles) {
        const Vec3& p0 = view[std::size_t(t[0])];
        const Vec3& p1 = view[std::size_t(t[1])];
        const Vec3& p2 = view[std::size_t(t[2])];
        const Vec3 n = (p1 - p0).cross(p2 - p0);
        const double len = n.length();
        if (len <= 0 || n.z <= 0) // facing away (outward, counter-clockwise seen from outside)
            continue;
        const Vec3 nn = n / len;
        Face f;
        f.depth = (p0.z + p1.z + p2.z) / 3;
        f.shade = 0.35 + 0.65 * std::max(0.0, nn.dot(light));
        f.polygon << center + QPointF(p0.x, -p0.y) * scale << center + QPointF(p1.x, -p1.y) * scale
                  << center + QPointF(p2.x, -p2.y) * scale;
        faces.push_back(std::move(f));
    }
    std::sort(faces.begin(), faces.end(), [](const Face& l, const Face& r) { return l.depth < r.depth; });
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const Face& f : faces) {
        const QColor shaded = QColor::fromRgbF(float(color_.redF() * f.shade + (1 - f.shade) * 0.10),
                                               float(color_.greenF() * f.shade + (1 - f.shade) * 0.12),
                                               float(color_.blueF() * f.shade + (1 - f.shade) * 0.16));
        painter->setPen(QPen(shaded, 0.6));
        painter->setBrush(shaded);
        painter->drawPolygon(f.polygon);
    }
}

} // namespace os::ui
