// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/SketchProfiles.h"

#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "geometry/TopoSignature.h"

#include <limits>

namespace os::doc {

geom::PlaneFrame planeFrame(const sketch::Plane& plane)
{
    return {plane.origin, plane.xAxis, plane.yAxis};
}

std::vector<geom::PlanarCurve> worldCurves(const sketch::Sketch& sketch, const sketch::Plane& plane)
{
    std::vector<geom::PlanarCurve> curves;
    for (const auto& [id, line] : sketch.lines()) {
        if (line.construction)
            continue;
        geom::PlanarCurve c;
        c.kind = geom::PlanarCurve::Kind::Segment;
        c.start = plane.toWorld(sketch.point(line.start)->position);
        c.end = plane.toWorld(sketch.point(line.end)->position);
        curves.push_back(c);
    }
    for (const auto& [id, circle] : sketch.circles()) {
        if (circle.construction)
            continue;
        geom::PlanarCurve c;
        c.kind = geom::PlanarCurve::Kind::Circle;
        c.center = plane.toWorld(sketch.point(circle.center)->position);
        c.radius = circle.radius;
        curves.push_back(c);
    }
    for (const auto& [id, arc] : sketch.arcs()) {
        if (arc.construction)
            continue;
        geom::PlanarCurve c;
        c.kind = geom::PlanarCurve::Kind::Arc;
        c.center = plane.toWorld(sketch.point(arc.center)->position);
        c.start = plane.toWorld(sketch.point(arc.start)->position);
        c.end = plane.toWorld(sketch.point(arc.end)->position);
        c.radius = sketch.arcRadius(id);
        curves.push_back(c);
    }
    return curves;
}

Result<std::vector<geom::Region>> sketchRegions(const sketch::Sketch& sketch)
{
    return sketchRegions(sketch, sketch.plane());
}

Result<std::vector<geom::Region>> sketchRegions(const sketch::Sketch& sketch, const sketch::Plane& plane)
{
    return geom::findRegions(planeFrame(plane), worldCurves(sketch, plane));
}

std::optional<int> resolveProfile(const std::vector<geom::Region>& regions, const sketch::Sketch& sketch,
                                  const ProfileRef& ref)
{
    return resolveProfile(regions, sketch.plane(), ref);
}

std::optional<int> resolveProfile(const std::vector<geom::Region>& regions, const sketch::Plane& plane,
                                  const ProfileRef& ref)
{
    const Vec3 point = plane.toWorld(ref.interiorPoint);
    std::optional<int> best;
    double bestScore = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (!geom::regionContains(regions[i].face, point))
            continue;
        // Regions are disjoint, so normally one contains the point; prefer the
        // one whose area matches best if tolerance makes several claim it.
        const double score = std::abs(regions[i].area - ref.area);
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

sketch::Plane effectivePlane(const sketch::Sketch& sketch, const EvalContext& context)
{
    // On a construction plane: where that plane is now (resolved with the
    // same context, so during a body recompute only earlier steps count).
    if (sketch.datumPlane()) {
        const Datum* datum = context.document ? context.document->datum(*sketch.datumPlane()) : nullptr;
        if (!datum || datum->kind() != DatumKind::Plane)
            return sketch.plane();
        const auto resolved = resolveDatum(*datum, context);
        return resolved ? sketchPlaneOn(resolved.value()) : sketch.plane();
    }
    const auto& attachment = sketch.attachment();
    if (!attachment)
        return sketch.plane();
    const Body* body = context.body && context.body->id() == attachment->body
                           ? context.body
                           : (context.document ? context.document->body(attachment->body) : nullptr);
    if (!body)
        return sketch.plane();
    const int index = body->featureIndex(attachment->feature);
    if (index < 0 || (body == context.body && index >= context.featureIndex))
        return sketch.plane();
    const FeatureState& state = body->state(index);
    if (state.status != FeatureStatus::Ok && state.status != FeatureStatus::Suppressed)
        return sketch.plane();
    const geom::FaceSignature signature{geom::SurfaceKind::Plane, attachment->faceNormal, attachment->faceCentroid,
                                        attachment->faceArea};
    const auto face = geom::resolveFace(state.output, signature, attachment->faceHint);
    const auto info = face ? geom::faceInfo(state.output, *face) : std::nullopt;
    if (!info || !info->isPlanar())
        return sketch.plane();
    const Vec3 n = info->normal.normalized();
    // Same construction as when the sketch was created: the world origin
    // projected onto the face plane, so sketch coordinates stay put.
    return sketch::Plane::fromNormal(n * n.dot(info->centroid), n);
}

std::optional<sketch::Attachment> makeAttachment(const Body& body, const Uuid& featureId, int faceIndex)
{
    const int index = body.featureIndex(featureId);
    if (index < 0)
        return std::nullopt;
    const auto info = geom::faceInfo(body.state(index).output, faceIndex);
    if (!info || !info->isPlanar())
        return std::nullopt;
    return sketch::Attachment{body.id(), featureId, faceIndex, info->normal, info->centroid, info->area};
}

ProfileRef makeProfileRef(const geom::Region& region, const sketch::Sketch& sketch)
{
    return {sketch.plane().toLocal(region.interiorPoint), region.area};
}

} // namespace os::doc
