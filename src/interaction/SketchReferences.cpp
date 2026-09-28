// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/SketchReferences.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/PlaneReferences.h"

#include <algorithm>
#include <cmath>

namespace os::interact {

namespace {

Vec2 nearestOnSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 d = b - a;
    const double t = std::clamp((p - a).dot(d) / std::max(d.dot(d), 1e-18), 0.0, 1.0);
    return a + d * t;
}

double ccwSweep(double from, double to)
{
    double d = std::fmod(to - from, 2 * kPi);
    return d < 0 ? d + 2 * kPi : d;
}

} // namespace

bool SketchReferences::nearestOnCurve(Vec2 p, Vec2& on, std::size_t& curve) const
{
    double best = 1e300;
    auto consider = [&](Vec2 q, std::size_t index) {
        if (const double d = (q - p).length(); d < best) {
            best = d;
            on = q;
            curve = index;
        }
    };
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const Curve& c = curves[i];
        switch (c.kind) {
        case Curve::Kind::Line: consider(nearestOnSegment(p, c.start, c.end), i); break;
        case Curve::Kind::Circle:
        case Curve::Kind::Arc: {
            const Vec2 d = p - c.center;
            if (d.length() < 1e-12 || c.radius < 1e-12)
                break;
            if (c.kind == Curve::Kind::Arc) {
                const double a0 = std::atan2(c.start.y - c.center.y, c.start.x - c.center.x);
                const double sweep = ccwSweep(a0, std::atan2(c.end.y - c.center.y, c.end.x - c.center.x));
                if (ccwSweep(a0, std::atan2(d.y, d.x)) > sweep) { // beside the arc: its nearer end
                    consider(c.start, i);
                    consider(c.end, i);
                    break;
                }
            }
            consider(c.center + d * (c.radius / d.length()), i);
            break;
        }
        case Curve::Kind::Polyline:
            for (std::size_t k = 1; k < c.points.size(); ++k)
                consider(nearestOnSegment(p, c.points[k - 1], c.points[k]), i);
            break;
        }
    }
    return best < 1e299;
}

SketchReferences sketchReferences(const doc::Document& document, const sketch::Plane& plane)
{
    const ScopedTimer timer("sketch references");
    SketchReferences out;
    const geom::PlaneFrame frame = doc::planeFrame(plane);
    auto addPoint = [&](Vec2 p, SketchReferences::PointKind kind) {
        for (const auto& existing : out.points)
            if ((existing.position - p).length() < 1e-6)
                return; // a corner shared by two edges, a hole's center twice
        out.points.push_back({p, kind});
    };
    for (const auto& body : document.bodies()) {
        if (!body->isVisible())
            continue;
        const geom::PlaneReferences refs = geom::planeReferences(body->shape(), frame);
        for (const auto& e : refs.edges) {
            SketchReferences::Curve c;
            c.start = plane.toLocal(e.start);
            c.end = plane.toLocal(e.end);
            c.center = plane.toLocal(e.center);
            c.radius = e.radius;
            switch (e.kind) {
            case geom::PlaneEdge::Kind::Line:
                c.kind = SketchReferences::Curve::Kind::Line;
                addPoint((c.start + c.end) * 0.5, SketchReferences::PointKind::Midpoint);
                break;
            case geom::PlaneEdge::Kind::Circle:
                c.kind = SketchReferences::Curve::Kind::Circle;
                addPoint(c.center, SketchReferences::PointKind::Center);
                break;
            case geom::PlaneEdge::Kind::Arc: {
                c.kind = SketchReferences::Curve::Kind::Arc;
                const double a0 = std::atan2(c.start.y - c.center.y, c.start.x - c.center.x);
                const double mid = a0 + ccwSweep(a0, std::atan2(c.end.y - c.center.y, c.end.x - c.center.x)) / 2;
                addPoint(c.center + Vec2{std::cos(mid), std::sin(mid)} * c.radius, SketchReferences::PointKind::Midpoint);
                addPoint(c.center, SketchReferences::PointKind::Center);
                break;
            }
            case geom::PlaneEdge::Kind::Polyline:
                c.kind = SketchReferences::Curve::Kind::Polyline;
                for (const Vec3& p : e.points)
                    c.points.push_back(plane.toLocal(p));
                break;
            }
            out.curves.push_back(std::move(c));
        }
        // Corners before the centers found above keep their kind (a vertex
        // on a hole's center is rare; a corner is what the user aims at).
        for (const Vec3& v : refs.vertices) {
            const Vec2 p = plane.toLocal(v);
            std::erase_if(out.points, [&](const SketchReferences::Point& q) { return (q.position - p).length() < 1e-6; });
            out.points.push_back({p, SketchReferences::PointKind::Corner});
        }
        for (const Vec3& c : refs.faceCenters)
            addPoint(plane.toLocal(c), SketchReferences::PointKind::Center);
    }
    OS_LOG(Debug, Sketch) << "sketch references: " << out.curves.size() << " curves, " << out.points.size() << " points";
    return out;
}

} // namespace os::interact
