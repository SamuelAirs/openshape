// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "app/FaceContrast.h"

#include "interaction/InteractionController.h"
#include "selection/Picking.h"

#include <QtCore/QStringList>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace os::app {
namespace {

// Items in paint order (by z, then declaration order), visible ones only.
void paintOrder(QQuickItem* item, std::vector<QQuickItem*>& out)
{
    if (!item->isVisible() || item->opacity() <= 0.0)
        return;
    out.push_back(item);
    QList<QQuickItem*> children = item->childItems();
    std::stable_sort(children.begin(), children.end(), [](const QQuickItem* a, const QQuickItem* b) { return a->z() < b->z(); });
    for (QQuickItem* child : children)
        paintOrder(child, out);
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0;
    const auto mid = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

struct FaceShade {
    std::vector<double> samples;
    bool skipped = false; // highlighted: its colour is not the lighting's
};

} // namespace

QString faceContrastReport(const QImage& image, QQuickWindow* window, const interact::InteractionController& interaction)
{
    const interact::RenderScene scene = interaction.renderScene();
    const Camera& camera = interaction.camera();
    QQuickItem* viewport = window->findChild<QQuickItem*>(QStringLiteral("viewport"));
    if (!viewport)
        return QStringLiteral("face contrast: no viewport");

    // What is painted over the 3D view: the items after it in paint order.
    std::vector<QQuickItem*> order;
    paintOrder(window->contentItem(), order);
    const auto self = std::find(order.begin(), order.end(), viewport);
    std::vector<QQuickItem*> overlays;
    for (auto it = self == order.end() ? order.end() : self + 1; it != order.end(); ++it)
        if ((*it)->flags().testFlag(QQuickItem::ItemHasContents))
            overlays.push_back(*it);
    auto covered = [&](QPointF p) {
        return std::any_of(overlays.begin(), overlays.end(), [&](QQuickItem* item) { return item->contains(item->mapFromScene(p)); });
    };
    const double dpr = image.devicePixelRatio();
    auto lumaAt = [&](QPointF p) {
        const int cx = int(std::lround(p.x() * dpr)), cy = int(std::lround(p.y() * dpr));
        if (cx < 1 || cy < 1 || cx + 1 >= image.width() || cy + 1 >= image.height())
            return -1.0;
        double sum = 0;
        for (int y = cy - 1; y <= cy + 1; ++y)
            for (int x = cx - 1; x <= cx + 1; ++x) {
                const QColor c = image.pixelColor(x, y);
                sum += 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
            }
        return sum / 9;
    };

    int measured = 0;
    std::vector<double> faceLumas;
    std::vector<double> contrasts;
    QStringList pairs;
    for (const auto& body : scene.bodies) {
        if (!body.mesh || body.selected || body.hovered)
            continue;
        const geom::Mesh& mesh = *body.mesh;
        const int faceCount = mesh.faceCount();
        std::vector<FaceShade> faces(std::size_t(std::max(faceCount, 0)));
        for (int f : body.selectedFaces)
            if (f >= 0 && f < faceCount)
                faces[std::size_t(f)].skipped = true;
        for (int f : body.highlightFaces)
            if (f >= 0 && f < faceCount)
                faces[std::size_t(f)].skipped = true;
        if (body.hoverFace >= 0 && body.hoverFace < faceCount)
            faces[std::size_t(body.hoverFace)].skipped = true;

        // Samples: points inside each face's triangles (about 40 triangles
        // per face at most), kept where that face is what the pixel shows.
        static const std::array<std::array<double, 3>, 7> kBarycentric{{{1 / 3.0, 1 / 3.0, 1 / 3.0},
                                                                        {0.6, 0.2, 0.2},
                                                                        {0.2, 0.6, 0.2},
                                                                        {0.2, 0.2, 0.6},
                                                                        {0.4, 0.4, 0.2},
                                                                        {0.4, 0.2, 0.4},
                                                                        {0.2, 0.4, 0.4}}};
        for (int f = 0; f < faceCount; ++f) {
            FaceShade& face = faces[std::size_t(f)];
            if (face.skipped)
                continue;
            const std::uint32_t first = mesh.faceTriangleOffset[std::size_t(f)], last = mesh.faceTriangleOffset[std::size_t(f) + 1];
            const std::uint32_t stride = std::max<std::uint32_t>(1, (last - first) / 40);
            for (std::uint32_t t = first; t < last; t += stride) {
                const Vec3 a = mesh.vertex(mesh.indices[3 * t]), b = mesh.vertex(mesh.indices[3 * t + 1]),
                           c = mesh.vertex(mesh.indices[3 * t + 2]);
                for (const auto& w : kBarycentric) {
                    const Vec3 p = a * w[0] + b * w[1] + c * w[2];
                    const Vec2 s = camera.project(p);
                    if (s.x < 4 || s.y < 4 || s.x > camera.viewportSize.x - 4 || s.y > camera.viewportSize.y - 4)
                        continue;
                    const QPointF scenePoint = viewport->mapToScene(QPointF(s.x, s.y));
                    if (covered(scenePoint))
                        continue;
                    // The app's own pick: an edge within 6 px (the edge lines) or
                    // a sketch's profile fill over the face (a tint) wins.
                    const sel::PickResult hit = interaction.pickAt(s, interact::InputProfile{});
                    if (hit.kind != sel::PickKind::Face || hit.bodyId != body.id || hit.index != f
                        || (hit.point - p).length() > 3 * camera.pixelSize(p))
                        continue;
                    if (const double l = lumaAt(scenePoint); l >= 0)
                        face.samples.push_back(l);
                }
            }
        }

        // Faces meeting along an edge (two or more shared mesh points), and
        // whether the edge is sharp there (normals more than 20 degrees apart).
        std::vector<int> vertexFace(mesh.vertexCount(), -1);
        for (int f = 0; f < faceCount; ++f)
            for (std::uint32_t t = mesh.faceTriangleOffset[std::size_t(f)]; t < mesh.faceTriangleOffset[std::size_t(f) + 1]; ++t)
                for (int k = 0; k < 3; ++k)
                    vertexFace[mesh.indices[3 * t + std::uint32_t(k)]] = f;
        std::map<std::array<long long, 3>, std::vector<std::size_t>> at; // micrometre grid -> vertices
        for (std::size_t v = 0; v < mesh.vertexCount(); ++v) {
            const Vec3 p = mesh.vertex(v);
            at[{std::llround(p.x * 1000), std::llround(p.y * 1000), std::llround(p.z * 1000)}].push_back(v);
        }
        struct Meeting {
            int sharp = 0, smooth = 0;
        };
        std::map<std::pair<int, int>, Meeting> meetings;
        const double kSharp = std::cos(20 * kPi / 180);
        for (const auto& [key, vertices] : at) {
            for (std::size_t i = 0; i < vertices.size(); ++i)
                for (std::size_t j = i + 1; j < vertices.size(); ++j) {
                    const int fa = vertexFace[vertices[i]], fb = vertexFace[vertices[j]];
                    if (fa < 0 || fb < 0 || fa == fb)
                        continue;
                    auto normal = [&](std::size_t v) {
                        return Vec3{mesh.normals[3 * v], mesh.normals[3 * v + 1], mesh.normals[3 * v + 2]}.normalized();
                    };
                    Meeting& m = meetings[{std::min(fa, fb), std::max(fa, fb)}];
                    (normal(vertices[i]).dot(normal(vertices[j])) < kSharp ? m.sharp : m.smooth)++;
                }
        }
        std::vector<double> shade(faces.size(), -1);
        for (std::size_t f = 0; f < faces.size(); ++f)
            if (faces[f].samples.size() >= 3) {
                shade[f] = median(faces[f].samples);
                faceLumas.push_back(shade[f]);
                ++measured;
            }
        for (const auto& [facesMeeting, m] : meetings) {
            const auto [fa, fb] = facesMeeting;
            if (m.sharp + m.smooth < 2 || m.sharp <= m.smooth)
                continue;
            if (shade[std::size_t(fa)] < 0 || shade[std::size_t(fb)] < 0)
                continue;
            const double difference = std::abs(shade[std::size_t(fa)] - shade[std::size_t(fb)]);
            contrasts.push_back(difference);
            pairs << QStringLiteral("%1/%2:%3").arg(fa).arg(fb).arg(difference, 0, 'f', 0);
        }
    }
    if (measured == 0)
        return QStringLiteral("face contrast: no face measured");
    std::sort(contrasts.begin(), contrasts.end());
    const int weak = int(std::count_if(contrasts.begin(), contrasts.end(), [](double c) { return c < 8; }));
    const auto [darkest, lightest] = std::minmax_element(faceLumas.begin(), faceLumas.end());
    QString report = QStringLiteral("face contrast: %1 faces, %2 sharp pairs").arg(measured).arg(contrasts.size());
    if (!contrasts.empty())
        report += QStringLiteral(", adjacent min %1 median %2, under 8: %3")
                      .arg(contrasts.front(), 0, 'f', 1)
                      .arg(median(contrasts), 0, 'f', 1)
                      .arg(weak);
    report += QStringLiteral(", faces %1-%2 (pairs %3)").arg(*darkest, 0, 'f', 0).arg(*lightest, 0, 'f', 0).arg(pairs.join(QLatin1Char(' ')));
    return report;
}

} // namespace os::app
