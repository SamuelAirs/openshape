#include "document/SketchProfiles.h"

#include <limits>

namespace os::doc {

geom::PlaneFrame planeFrame(const sketch::Plane& plane)
{
    return {plane.origin, plane.xAxis, plane.yAxis};
}

std::vector<geom::PlanarCurve> worldCurves(const sketch::Sketch& sketch)
{
    const sketch::Plane& plane = sketch.plane();
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
    return curves;
}

Result<std::vector<geom::Region>> sketchRegions(const sketch::Sketch& sketch)
{
    return geom::findRegions(planeFrame(sketch.plane()), worldCurves(sketch));
}

std::optional<int> resolveProfile(const std::vector<geom::Region>& regions, const sketch::Sketch& sketch,
                                  const ProfileRef& ref)
{
    const Vec3 point = sketch.plane().toWorld(ref.interiorPoint);
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

ProfileRef makeProfileRef(const geom::Region& region, const sketch::Sketch& sketch)
{
    return {sketch.plane().toLocal(region.interiorPoint), region.area};
}

} // namespace os::doc
