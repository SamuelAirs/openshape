// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "core/Result.h"
#include "core/Uuid.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace os::sketch {

// Entity ids are unique and never reused within one sketch (the sketch
// itself has a UUID). Points, lines, circles and constraints share one id space.
using EntityId = std::uint32_t;
inline constexpr EntityId kNoEntity = 0;
inline constexpr EntityId kOriginId = 1; // fixed point at the plane origin, always present

// A sketch plane with an orthonormal 2D frame. Sketch coordinates are
// millimeters in (xAxis, yAxis) from `origin`.
struct Plane {
    Vec3 origin{0, 0, 0};
    Vec3 xAxis{1, 0, 0};
    Vec3 yAxis{0, 1, 0};

    Vec3 normal() const { return xAxis.cross(yAxis); }
    Vec3 toWorld(Vec2 p) const { return origin + xAxis * p.x + yAxis * p.y; }
    Vec2 toLocal(const Vec3& w) const
    {
        const Vec3 d = w - origin;
        return {d.dot(xAxis), d.dot(yAxis)};
    }
    std::optional<Vec2> intersect(const Ray& ray) const;

    static Plane xy() { return {}; }
    // Builds a frame for any normal; the x axis is kept horizontal when possible.
    static Plane fromNormal(const Vec3& origin, const Vec3& normal);
};

struct SketchPoint {
    Vec2 position;
    bool fixed = false;
};

struct SketchLine {
    EntityId start = kNoEntity;
    EntityId end = kNoEntity;
    bool construction = false;
};

struct SketchCircle {
    EntityId center = kNoEntity;
    double radius = 1.0;
    bool construction = false;
};

// A circular arc, counterclockwise (in sketch coordinates) from `start` to
// `end` around `center`. Its radius is the distance from center to start;
// the solver keeps the end on the same circle.
struct SketchArc {
    EntityId center = kNoEntity;
    EntityId start = kNoEntity;
    EntityId end = kNoEntity;
    bool construction = false;
};

enum class ConstraintKind {
    Coincident,         // points a, b
    Horizontal,         // line a
    Vertical,           // line a
    Distance,           // points a, b; value > 0
    HorizontalDistance, // points a, b; value = b.x - a.x (signed)
    VerticalDistance,   // points a, b; value = b.y - a.y (signed)
    Diameter,           // circle a; value > 0
    Parallel,           // lines a, b
    Perpendicular,      // lines a, b
    Equal,              // lines a, b (same length) or circles a, b (same radius)
    Tangent,            // line or circle a, circle b
    Concentric,         // circles a, b
    PointOnLine,        // point a on the (infinite) line b
    Midpoint,           // point a at the middle of line b
    Radius,             // arc a; value > 0
    PointOnCircle,      // point a on the (full) circle of circle or arc b
    Symmetric,          // points a, b mirror images across the (infinite) line c
};

struct SketchConstraint {
    ConstraintKind kind = ConstraintKind::Coincident;
    EntityId a = kNoEntity;
    EntityId b = kNoEntity;
    double value = 0;
    EntityId c = kNoEntity; // a third entity (Symmetric: the line mirrored across)

    bool isDimension() const
    {
        return kind == ConstraintKind::Distance || kind == ConstraintKind::HorizontalDistance
            || kind == ConstraintKind::VerticalDistance || kind == ConstraintKind::Diameter || kind == ConstraintKind::Radius;
    }
};

// A sketch placed on a planar face follows that face when the body changes.
// The face is identified like any persistent face reference: the feature
// whose output it belongs to, an index hint, and a geometric signature.
struct Attachment {
    Uuid body;
    Uuid feature;
    int faceHint = -1;
    Vec3 faceNormal;
    Vec3 faceCentroid;
    double faceArea = 0;
};

struct SolveReport {
    bool ok = true;
    int degreesOfFreedom = -1;
    std::vector<EntityId> conflicting; // constraint ids
    std::vector<EntityId> redundant;   // constraint ids
    std::string message;               // user-facing when !ok
};

// A 2D sketch on a plane: points, lines, circles and constraints.
// Positions stored here are always the last solved state.
class Sketch {
public:
    explicit Sketch(Uuid id = Uuid::generate(), Plane plane = Plane::xy());

    const Uuid& id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    const Plane& plane() const { return plane_; }
    void setPlane(const Plane& plane) { plane_ = plane; }
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    // Body whose face the sketch was placed on (used to choose join/cut).
    const std::optional<Uuid>& hostBody() const { return hostBody_; }
    void setHostBody(std::optional<Uuid> body) { hostBody_ = body; }
    const std::optional<Attachment>& attachment() const { return attachment_; }
    void setAttachment(std::optional<Attachment> attachment) { attachment_ = std::move(attachment); }

    // ---- Entities ----
    EntityId addPoint(Vec2 position, bool fixed = false);
    EntityId addLine(EntityId start, EntityId end, bool construction = false);
    EntityId addCircle(EntityId center, double radius, bool construction = false);
    // The three points must exist and differ; start and end should already lie
    // on a circle around center (the solver keeps them there).
    EntityId addArc(EntityId center, EntityId start, EntityId end, bool construction = false);
    EntityId addConstraint(const SketchConstraint& constraint);
    // Marks a line or circle as construction (never part of a profile).
    bool setConstruction(EntityId id, bool construction);
    // True if the constraint references suitable existing entities and has a valid value.
    bool isValid(const SketchConstraint& constraint) const;
    // Removes an entity and everything that depends on it (lines using a
    // removed point, constraints referencing removed entities, points no
    // longer used by anything). The origin cannot be removed.
    bool remove(EntityId id);

    const std::map<EntityId, SketchPoint>& points() const { return points_; }
    const std::map<EntityId, SketchLine>& lines() const { return lines_; }
    const std::map<EntityId, SketchCircle>& circles() const { return circles_; }
    const std::map<EntityId, SketchArc>& arcs() const { return arcs_; }
    const std::map<EntityId, SketchConstraint>& constraints() const { return constraints_; }

    const SketchPoint* point(EntityId id) const;
    const SketchLine* line(EntityId id) const;
    const SketchCircle* circle(EntityId id) const;
    const SketchArc* arc(EntityId id) const;
    // Distance from an arc's center to its start (0 for unknown ids).
    double arcRadius(EntityId id) const;
    // Circles and arcs.
    bool isRound(EntityId id) const { return circles_.contains(id) || arcs_.contains(id); }
    const SketchConstraint* constraint(EntityId id) const;
    SketchPoint* point(EntityId id);
    SketchLine* line(EntityId id);
    SketchCircle* circle(EntityId id);
    SketchArc* arc(EntityId id);
    SketchConstraint* constraint(EntityId id);

    // Constraints that mention an entity directly.
    std::vector<EntityId> constraintsOn(EntityId id) const;
    bool hasGeometry() const { return !lines_.empty() || !circles_.empty() || !arcs_.empty(); }

    // Last solver report (not persisted; recomputed by solve()).
    const SolveReport& solveReport() const { return report_; }
    void setSolveReport(SolveReport report) { report_ = std::move(report); }

    nlohmann::json toJson() const;
    static Result<Sketch> fromJson(const nlohmann::json& json);

private:
    EntityId allocate() { return nextId_++; }
    bool exists(EntityId id) const;

    Uuid id_;
    std::string name_;
    Plane plane_;
    bool visible_ = true;
    std::optional<Uuid> hostBody_;
    std::optional<Attachment> attachment_;
    std::map<EntityId, SketchPoint> points_;
    std::map<EntityId, SketchLine> lines_;
    std::map<EntityId, SketchCircle> circles_;
    std::map<EntityId, SketchArc> arcs_;
    std::map<EntityId, SketchConstraint> constraints_;
    EntityId nextId_ = kOriginId + 1;
    SolveReport report_;
};

// Convenience builders used by tools and tests. They add geometry plus the
// constraints that express the drawn intent.
struct RectangleIds {
    EntityId corners[4]{};  // A, (B.x, A.y), B, (A.x, B.y)
    EntityId edges[4]{};    // bottom, right, top, left (for an axis-aligned drag)
};
RectangleIds addRectangle(Sketch& sketch, Vec2 cornerA, Vec2 cornerB, EntityId reuseFirstCorner = kNoEntity);

// Solves in place. Returns the report (also stored on the sketch). On
// failure the previous positions are kept.
SolveReport solve(Sketch& sketch);
// Solves while pulling `pointId` towards `target` (interactive dragging).
SolveReport solveDragging(Sketch& sketch, EntityId pointId, Vec2 target);

} // namespace os::sketch
