// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Loft through flat profiles (BRepOffsetAPI_ThruSections), with the checks
// that make a kernel "done" trustworthy: the surface must not cross itself
// and the end faces must be exactly the first and last profile.

#include "geometry/Loft.h"

#include "core/Timer.h"
#include "geometry/Modeling.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace os::geom {

using namespace detail;

namespace {

constexpr const char* kLoftFailed = "Unable to loft these profiles. Try profiles that are more alike, or Straight.";
constexpr const char* kCrossesItself =
    "The loft would cross through itself. Change the order of the profiles, or move them apart.";

// One closed loop of a profile: its wire, and the area and middle of the
// region it encloses (holes not taken out).
struct Loop {
    TopoDS_Wire wire;
    double area = 0;
    Vec3 centroid;
};

struct Profile {
    gp_Pln plane;
    Loop outer;
    std::vector<Loop> holes;
    double area = 0; // the profile itself: outer minus holes
};

std::optional<Loop> loopOf(const TopoDS_Wire& wire, const gp_Pln& plane)
{
    // Inside = true: the face is the finite region the loop encloses,
    // whichever way the wire runs (a hole's wire runs the other way).
    BRepBuilderAPI_MakeFace maker(plane, wire, Standard_True);
    if (!maker.IsDone())
        return std::nullopt;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(maker.Face(), props);
    return Loop{wire, std::abs(props.Mass()), fromPnt(props.CentreOfMass())};
}

bool coplanar(const gp_Pln& a, const gp_Pln& b)
{
    return a.Axis().Direction().IsParallel(b.Axis().Direction(), 1e-9) && a.Distance(b.Location()) < kMinLength;
}

using Solid = Result<TopoDS_Shape>;

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

bool sameValue(double a, double b)
{
    return std::abs(a - b) <= 1e-6 * std::max({std::abs(a), std::abs(b), 1.0});
}

// ThruSections carries every side on a B-spline surface, also the flat ones
// (between parallel straight edges: a pyramid frustum's sides). Those become
// plane faces, so push/pull, sketches, holes and text take them as the flat
// faces they are. The faces are sewn into a solid again; if anything about
// that is off (not one valid solid, another volume), the loft stays as the
// kernel made it.
TopoDS_Shape flattenPlanarFaces(const TopoDS_Shape& solid)
{
    std::vector<TopoDS_Face> faces;
    bool flattened = false;
    for (TopExp_Explorer ex(solid, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        faces.push_back(face);
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane)
            continue;
        const GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face), 1e-7);
        if (!planar.IsPlanar())
            continue;
        TopoDS_Face flat;
        for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next()) {
            if (flat.IsNull()) {
                BRepBuilderAPI_MakeFace maker(planar.Plan(), TopoDS::Wire(w.Current()), Standard_True);
                if (maker.IsDone())
                    flat = maker.Face();
            } else {
                flat.Nullify(); // a side face with a hole: not expected, left alone
                break;
            }
        }
        if (flat.IsNull())
            continue;
        faces.back() = flat;
        flattened = true;
    }
    if (!flattened)
        return solid;
    BRepBuilderAPI_Sewing sewing(1e-6);
    for (const TopoDS_Face& face : faces)
        sewing.Add(face);
    sewing.Perform();
    TopoDS_Shell shell;
    int shells = 0;
    for (TopExp_Explorer ex(sewing.SewedShape(), TopAbs_SHELL); ex.More(); ex.Next(), ++shells)
        shell = TopoDS::Shell(ex.Current());
    if (shells != 1)
        return solid;
    BRepBuilderAPI_MakeSolid maker(shell);
    if (!maker.IsDone())
        return solid;
    TopoDS_Solid sewn = maker.Solid();
    BRepLib::OrientClosedSolid(sewn);
    if (!BRepCheck_Analyzer(sewn).IsValid() || !sameValue(volumeOf(sewn), volumeOf(solid))) {
        OS_LOG(Debug, Kernel) << "loft: flat sides not taken (sewn solid invalid or of another volume)";
        return solid;
    }
    return sewn;
}

// The single solid of a loft, oriented so its volume is positive.
Solid loftLoops(const std::vector<TopoDS_Wire>& wires, bool ruled)
{
    BRepOffsetAPI_ThruSections loft(Standard_True, ruled ? Standard_True : Standard_False, 1e-6);
    // Our region faces are shared (cached step inputs, previews): never
    // changed in place.
    loft.SetMutableInput(Standard_False);
    // Starting points and directions chosen to avoid twisting; loops with
    // different numbers of edges are split to match (square to circle).
    loft.CheckCompatibility(Standard_True);
    for (const TopoDS_Wire& wire : wires)
        loft.AddWire(wire);
    loft.Build();
    if (!loft.IsDone())
        return Solid::failure(ErrorCode::KernelFailure, kLoftFailed,
                              "BRepOffsetAPI_ThruSections not done (status "
                                  + std::to_string(static_cast<int>(loft.GetStatus())) + ")");
    for (TopExp_Explorer ex(loft.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
        TopoDS_Solid solid = TopoDS::Solid(ex.Current());
        BRepLib::OrientClosedSolid(solid);
        return Solid::success(flattenPlanarFaces(solid));
    }
    return Solid::failure(ErrorCode::KernelFailure, kLoftFailed, "BRepOffsetAPI_ThruSections made no solid");
}

// The total area of the loft's flat faces lying in `plane`.
double areaIn(const TopoDS_Shape& solid, const gp_Pln& plane)
{
    double area = 0;
    for (TopExp_Explorer ex(solid, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane || !coplanar(surface.Plane(), plane))
            continue;
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        area += std::abs(props.Mass());
    }
    return area;
}

// A lofted loop sequence must be a sound solid: its surface does not cross
// itself, and its end faces are exactly the first and last loop.
Status checkLoft(const TopoDS_Shape& solid, const std::vector<const Loop*>& loops, const std::vector<gp_Pln>& planes)
{
    const std::size_t last = loops.size() - 1;
    for (std::size_t end : {std::size_t(0), last}) {
        // A later profile in the same plane (an arch that comes back down)
        // puts its end face there too.
        double expected = loops[end]->area;
        const std::size_t other = end == 0 ? last : 0;
        if (coplanar(planes[end], planes[other]))
            expected += loops[other]->area;
        const double found = areaIn(solid, planes[end]);
        if (!sameValue(found, expected))
            return Status::failure(ErrorCode::InvalidResultShape, kLoftFailed,
                                   "loft end face " + std::to_string(end) + ": area " + std::to_string(found) + " instead of "
                                       + std::to_string(expected));
    }
    if (const double v = volumeOf(solid); !(v > kMinLength))
        return Status::failure(ErrorCode::InvalidResultShape, kCrossesItself, "loft volume " + std::to_string(v));
    // Valid topology, and no face crossing another (BOPAlgo_ArgumentAnalyzer).
    BRepAlgoAPI_Check check(solid, /*bTestSE=*/Standard_False, /*bTestSI=*/Standard_True);
    if (!check.IsValid())
        return Status::failure(ErrorCode::InvalidResultShape, kCrossesItself, "loft fails BRepAlgoAPI_Check (self-interference)");
    return okStatus();
}

} // namespace

Result<Shape> loftFaces(const std::vector<Shape>& profileFaces, bool ruled)
{
    using R = Result<Shape>;
    if (profileFaces.size() < 2)
        return R::failure(ErrorCode::InvalidArgument, "Select two or more closed profiles to loft.",
                          "loftFaces: " + std::to_string(profileFaces.size()) + " profiles");
    return guarded("loftFaces", kLoftFailed, [&]() -> R {
        ScopedTimer timer("loftFaces");
        std::vector<Profile> profiles;
        for (const Shape& shape : profileFaces) {
            int faces = 0;
            TopoDS_Face face;
            for (TopExp_Explorer ex(occ(shape), TopAbs_FACE); ex.More(); ex.Next(), ++faces)
                face = TopoDS::Face(ex.Current());
            if (faces != 1)
                return R::failure(ErrorCode::InvalidArgument, "Each profile to loft must be one closed shape.",
                                  "loftFaces: a profile has " + std::to_string(faces) + " faces");
            const BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane)
                return R::failure(ErrorCode::NotPlanar, "Loft profiles must be flat.", "loftFaces: profile not planar");
            Profile p;
            p.plane = surface.Plane();
            const TopoDS_Wire outer = BRepTools::OuterWire(face);
            for (TopExp_Explorer ex(face, TopAbs_WIRE); ex.More(); ex.Next()) {
                const TopoDS_Wire wire = TopoDS::Wire(ex.Current());
                const auto loop = loopOf(wire, p.plane);
                if (!loop)
                    return R::failure(ErrorCode::KernelFailure, kLoftFailed, "loftFaces: no face from a profile loop");
                if (wire.IsSame(outer))
                    p.outer = *loop;
                else
                    p.holes.push_back(*loop);
            }
            if (p.outer.wire.IsNull())
                return R::failure(ErrorCode::KernelFailure, kLoftFailed, "loftFaces: profile without an outer loop");
            p.area = p.outer.area;
            for (const Loop& hole : p.holes)
                p.area -= hole.area;
            profiles.push_back(std::move(p));
        }

        for (std::size_t i = 1; i < profiles.size(); ++i) {
            if (coplanar(profiles[i - 1].plane, profiles[i].plane))
                return R::failure(ErrorCode::InvalidArgument,
                                  "Two profiles in a row lie in the same plane. A loft joins profiles on different "
                                  "planes: sketch the next one on a construction plane.",
                                  "loftFaces: profiles " + std::to_string(i - 1) + " and " + std::to_string(i) + " coplanar");
            if (profiles[i].holes.size() != profiles[0].holes.size())
                return R::failure(ErrorCode::InvalidArgument,
                                  "Every profile of a loft needs the same number of holes (" + std::to_string(profiles[0].holes.size())
                                      + " in the first, " + std::to_string(profiles[i].holes.size()) + " in profile "
                                      + std::to_string(i + 1) + ").",
                                  "loftFaces: hole counts differ");
        }
        // Pair each hole with the nearest one in the profile before (seen
        // from each profile's middle): holes are listed in that order.
        for (std::size_t i = 1; i < profiles.size(); ++i) {
            const Profile& before = profiles[i - 1];
            Profile& now = profiles[i];
            std::vector<Loop> ordered;
            std::vector<bool> taken(now.holes.size(), false);
            for (const Loop& previous : before.holes) {
                const Vec3 offset = previous.centroid - before.outer.centroid;
                std::size_t best = 0;
                double bestDistance = std::numeric_limits<double>::max();
                for (std::size_t k = 0; k < now.holes.size(); ++k) {
                    const double d = ((now.holes[k].centroid - now.outer.centroid) - offset).length();
                    if (!taken[k] && d < bestDistance) {
                        bestDistance = d;
                        best = k;
                    }
                }
                taken[best] = true;
                ordered.push_back(now.holes[best]);
            }
            now.holes = std::move(ordered);
        }

        std::vector<gp_Pln> planes;
        for (const Profile& p : profiles)
            planes.push_back(p.plane);
        // The loft through one loop of every profile (`loopAt` picks it), checked.
        auto loftOf = [&](auto loopAt) -> Solid {
            std::vector<TopoDS_Wire> wires;
            std::vector<const Loop*> loops;
            for (const Profile& p : profiles) {
                loops.push_back(&loopAt(p));
                wires.push_back(loopAt(p).wire);
            }
            Solid solid = loftLoops(wires, ruled);
            if (!solid)
                return solid;
            if (Status sound = checkLoft(solid.value(), loops, planes); !sound)
                return Solid::failureFrom(sound);
            return solid;
        };

        const Solid outside = loftOf([](const Profile& p) -> const Loop& { return p.outer; });
        if (!outside)
            return R::failureFrom(outside);
        Shape result = makeShape(outside.value());
        if (!profiles[0].holes.empty()) {
            double removed = 0;
            for (std::size_t k = 0; k < profiles[0].holes.size(); ++k) {
                const Solid inside = loftOf([k](const Profile& p) -> const Loop& { return p.holes[k]; });
                if (!inside)
                    return R::failureFrom(inside);
                const Shape tool = makeShape(inside.value());
                removed += volume(tool);
                auto cut = booleanOp(result, tool, BooleanKind::Subtract);
                if (!cut)
                    return R::failure(ErrorCode::KernelFailure, kLoftFailed, cut.developerMessage());
                result = cut.value();
            }
            // Each hole's loft stays inside: exactly its volume goes.
            const double expected = volume(makeShape(outside.value())) - removed;
            if (!sameValue(volume(result), expected) || result.solidCount() != 1)
                return R::failure(ErrorCode::InvalidResultShape,
                                  "A hole's loft would break through the outside of the loft. Make the holes smaller, "
                                  "or keep them further from the edge.",
                                  "loftFaces: volume " + std::to_string(volume(result)) + " instead of "
                                      + std::to_string(expected));
        }
        return finishSolid(occ(result), "loftFaces", kLoftFailed);
    });
}

} // namespace os::geom
