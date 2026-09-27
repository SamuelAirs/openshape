// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sketch/Sketch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace os::sketch {

using nlohmann::json;

std::optional<Vec2> Plane::intersect(const Ray& ray) const
{
    const Vec3 n = normal();
    const double denom = ray.direction.dot(n);
    if (std::abs(denom) < 1e-12)
        return std::nullopt;
    const double t = (origin - ray.origin).dot(n) / denom;
    return toLocal(ray.at(t));
}

Plane Plane::fromNormal(const Vec3& origin, const Vec3& normal)
{
    const Vec3 n = normal.normalized();
    Plane p;
    p.origin = origin;
    // Horizontal x axis for walls; world X for floors/ceilings.
    Vec3 x = Vec3{0, 0, 1}.cross(n);
    if (x.length() < 1e-6)
        x = n.z > 0 ? Vec3{1, 0, 0} : Vec3{-1, 0, 0};
    p.xAxis = x.normalized();
    p.yAxis = n.cross(p.xAxis).normalized();
    return p;
}

Sketch::Sketch(Uuid id, Plane plane) : id_(id), plane_(plane)
{
    points_[kOriginId] = SketchPoint{{0, 0}, true};
}

bool Sketch::exists(EntityId id) const
{
    return points_.contains(id) || lines_.contains(id) || circles_.contains(id) || arcs_.contains(id)
        || constraints_.contains(id);
}

EntityId Sketch::addPoint(Vec2 position, bool fixed)
{
    const EntityId id = allocate();
    points_[id] = SketchPoint{position, fixed};
    return id;
}

EntityId Sketch::addLine(EntityId start, EntityId end, bool construction)
{
    if (!points_.contains(start) || !points_.contains(end) || start == end)
        return kNoEntity;
    const EntityId id = allocate();
    lines_[id] = SketchLine{start, end, construction};
    return id;
}

EntityId Sketch::addCircle(EntityId center, double radius, bool construction)
{
    if (!points_.contains(center) || !(radius > 0))
        return kNoEntity;
    const EntityId id = allocate();
    circles_[id] = SketchCircle{center, radius, construction};
    return id;
}

EntityId Sketch::addArc(EntityId center, EntityId start, EntityId end, bool construction)
{
    if (!points_.contains(center) || !points_.contains(start) || !points_.contains(end) || center == start
        || center == end || start == end)
        return kNoEntity;
    if (!((points_.at(start).position - points_.at(center).position).length() > 0))
        return kNoEntity;
    const EntityId id = allocate();
    arcs_[id] = SketchArc{center, start, end, construction};
    return id;
}

const SketchArc* Sketch::arc(EntityId id) const
{
    auto it = arcs_.find(id);
    return it == arcs_.end() ? nullptr : &it->second;
}

double Sketch::arcRadius(EntityId id) const
{
    const SketchArc* a = arc(id);
    return a ? (points_.at(a->start).position - points_.at(a->center).position).length() : 0.0;
}

bool Sketch::setConstruction(EntityId id, bool construction)
{
    if (auto it = lines_.find(id); it != lines_.end()) {
        it->second.construction = construction;
        return true;
    }
    if (auto it = circles_.find(id); it != circles_.end()) {
        it->second.construction = construction;
        return true;
    }
    if (auto it = arcs_.find(id); it != arcs_.end()) {
        it->second.construction = construction;
        return true;
    }
    return false;
}

bool Sketch::isValid(const SketchConstraint& c) const
{
    auto isPoint = [&](EntityId e) { return points_.contains(e); };
    bool valid = false;
    switch (c.kind) {
    case ConstraintKind::Coincident:
    case ConstraintKind::Distance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
        valid = isPoint(c.a) && isPoint(c.b) && c.a != c.b;
        break;
    case ConstraintKind::Horizontal:
    case ConstraintKind::Vertical:
        valid = lines_.contains(c.a);
        break;
    case ConstraintKind::Diameter:
        valid = circles_.contains(c.a);
        break;
    case ConstraintKind::Parallel:
    case ConstraintKind::Perpendicular:
        valid = lines_.contains(c.a) && lines_.contains(c.b) && c.a != c.b;
        break;
    case ConstraintKind::Equal:
        valid = c.a != c.b && ((lines_.contains(c.a) && lines_.contains(c.b)) || (isRound(c.a) && isRound(c.b)));
        break;
    case ConstraintKind::Tangent:
        valid = c.a != c.b && (lines_.contains(c.a) || isRound(c.a)) && isRound(c.b);
        break;
    case ConstraintKind::Concentric:
        valid = c.a != c.b && isRound(c.a) && isRound(c.b);
        break;
    case ConstraintKind::Radius:
        valid = arcs_.contains(c.a);
        break;
    case ConstraintKind::PointOnLine:
    case ConstraintKind::Midpoint:
        // An endpoint is on its line already: that would only be redundant.
        valid = isPoint(c.a) && lines_.contains(c.b) && lines_.at(c.b).start != c.a && lines_.at(c.b).end != c.a;
        break;
    case ConstraintKind::PointOnCircle:
        // Not the center, and not an arc's own end (already on it).
        valid = isPoint(c.a) && isRound(c.b)
             && (!circles_.contains(c.b) || circles_.at(c.b).center != c.a)
             && (!arcs_.contains(c.b)
                 || (arcs_.at(c.b).center != c.a && arcs_.at(c.b).start != c.a && arcs_.at(c.b).end != c.a));
        break;
    case ConstraintKind::Angle:
        valid = lines_.contains(c.a) && lines_.contains(c.b) && c.a != c.b;
        break;
    case ConstraintKind::Symmetric:
        // Two different points, neither an end of the line (a point on the
        // line is its own mirror image).
        valid = isPoint(c.a) && isPoint(c.b) && c.a != c.b && lines_.contains(c.c)
             && lines_.at(c.c).start != c.a && lines_.at(c.c).end != c.a && lines_.at(c.c).start != c.b
             && lines_.at(c.c).end != c.b;
        break;
    }
    if (valid && c.kind != ConstraintKind::Symmetric && c.c != kNoEntity)
        return false; // only Symmetric uses a third entity
    if (!valid || !std::isfinite(c.value))
        return false;
    if ((c.kind == ConstraintKind::Distance || c.kind == ConstraintKind::Diameter || c.kind == ConstraintKind::Radius)
        && !(c.value > 0))
        return false;
    return true;
}

EntityId Sketch::addConstraint(const SketchConstraint& c)
{
    if (!isValid(c))
        return kNoEntity;
    const EntityId id = allocate();
    constraints_[id] = c;
    return id;
}

bool Sketch::remove(EntityId id)
{
    if (id == kOriginId || !exists(id))
        return false;
    if (constraints_.erase(id))
        return true;

    std::vector<EntityId> removedPoints;
    if (points_.erase(id)) {
        removedPoints.push_back(id);
        std::erase_if(lines_, [&](const auto& l) { return l.second.start == id || l.second.end == id; });
        std::erase_if(circles_, [&](const auto& c) { return c.second.center == id; });
        std::erase_if(arcs_, [&](const auto& a) {
            const SketchArc& arc = a.second;
            if (arc.center != id && arc.start != id && arc.end != id)
                return false;
            // The arc's other points may become unused.
            for (EntityId p : {arc.center, arc.start, arc.end})
                if (p != id)
                    removedPoints.push_back(p);
            return true;
        });
    } else if (auto at = arcs_.find(id); at != arcs_.end()) {
        const SketchArc arc = at->second;
        arcs_.erase(at);
        removedPoints = {arc.center, arc.start, arc.end};
    } else if (auto it = lines_.find(id); it != lines_.end()) {
        const SketchLine line = it->second;
        lines_.erase(it);
        removedPoints = {line.start, line.end};
    } else if (auto ct = circles_.find(id); ct != circles_.end()) {
        const EntityId center = ct->second.center;
        circles_.erase(ct);
        removedPoints = {center};
    }

    // Drop points no longer used by any curve (except the origin).
    for (EntityId p : removedPoints) {
        if (p == kOriginId || !points_.contains(p))
            continue;
        const bool used = std::any_of(lines_.begin(), lines_.end(),
                                      [&](const auto& l) { return l.second.start == p || l.second.end == p; })
            || std::any_of(circles_.begin(), circles_.end(), [&](const auto& c) { return c.second.center == p; })
            || std::any_of(arcs_.begin(), arcs_.end(), [&](const auto& a) {
                   return a.second.center == p || a.second.start == p || a.second.end == p;
               });
        if (!used)
            points_.erase(p);
    }
    // Drop constraints that reference anything that no longer exists.
    std::erase_if(constraints_, [&](const auto& c) {
        const SketchConstraint& k = c.second;
        auto gone = [&](EntityId e) {
            return e != kNoEntity && !points_.contains(e) && !lines_.contains(e) && !circles_.contains(e)
                && !arcs_.contains(e);
        };
        return gone(k.a) || gone(k.b) || gone(k.c);
    });
    return true;
}

const SketchPoint* Sketch::point(EntityId id) const
{
    auto it = points_.find(id);
    return it == points_.end() ? nullptr : &it->second;
}
SketchPoint* Sketch::point(EntityId id)
{
    auto it = points_.find(id);
    return it == points_.end() ? nullptr : &it->second;
}
const SketchLine* Sketch::line(EntityId id) const
{
    auto it = lines_.find(id);
    return it == lines_.end() ? nullptr : &it->second;
}
const SketchCircle* Sketch::circle(EntityId id) const
{
    auto it = circles_.find(id);
    return it == circles_.end() ? nullptr : &it->second;
}
SketchCircle* Sketch::circle(EntityId id)
{
    auto it = circles_.find(id);
    return it == circles_.end() ? nullptr : &it->second;
}
SketchLine* Sketch::line(EntityId id)
{
    auto it = lines_.find(id);
    return it == lines_.end() ? nullptr : &it->second;
}
SketchArc* Sketch::arc(EntityId id)
{
    auto it = arcs_.find(id);
    return it == arcs_.end() ? nullptr : &it->second;
}
const SketchConstraint* Sketch::constraint(EntityId id) const
{
    auto it = constraints_.find(id);
    return it == constraints_.end() ? nullptr : &it->second;
}
SketchConstraint* Sketch::constraint(EntityId id)
{
    auto it = constraints_.find(id);
    return it == constraints_.end() ? nullptr : &it->second;
}

std::vector<EntityId> Sketch::constraintsOn(EntityId id) const
{
    std::vector<EntityId> out;
    for (const auto& [cid, c] : constraints_)
        if (c.a == id || c.b == id || c.c == id)
            out.push_back(cid);
    return out;
}

namespace {

struct LinePair {
    Vec2 a0, da, b0, db; // start and direction (start -> end) of each line
};

std::optional<LinePair> linePair(const Sketch& s, EntityId lineA, EntityId lineB)
{
    const auto* a = s.line(lineA);
    const auto* b = s.line(lineB);
    if (!a || !b)
        return std::nullopt;
    LinePair p{s.point(a->start)->position, s.point(a->end)->position - s.point(a->start)->position,
               s.point(b->start)->position, s.point(b->end)->position - s.point(b->start)->position};
    if (p.da.length() < 1e-12 || p.db.length() < 1e-12)
        return std::nullopt;
    return p;
}

double signedAngle(Vec2 from, Vec2 to)
{
    return std::atan2(from.x * to.y - from.y * to.x, from.dot(to));
}

// Where they meet, as parameters along each line (0 at the start, 1 at the end).
std::optional<std::pair<double, double>> meeting(const LinePair& p)
{
    const double den = p.da.x * p.db.y - p.da.y * p.db.x;
    if (std::abs(den) < 1e-9 * p.da.length() * p.db.length())
        return std::nullopt;
    const Vec2 w = p.b0 - p.a0;
    return std::make_pair((w.x * p.db.y - w.y * p.db.x) / den, (w.x * p.da.y - w.y * p.da.x) / den);
}

// Whether each line's direction points from the meeting point towards the
// line's middle (+1) or away from it (-1). nullopt when parallel.
std::optional<std::pair<double, double>> raySides(const LinePair& p)
{
    const auto m = meeting(p);
    if (!m)
        return std::nullopt;
    return std::make_pair(m->first <= 0.5 ? 1.0 : -1.0, m->second <= 0.5 ? 1.0 : -1.0);
}

} // namespace

std::optional<double> lineDirectionAngle(const Sketch& s, EntityId lineA, EntityId lineB)
{
    const auto p = linePair(s, lineA, lineB);
    if (!p)
        return std::nullopt;
    return signedAngle(p->da, p->db);
}

std::optional<Vec2> lineIntersection(const Sketch& s, EntityId lineA, EntityId lineB)
{
    const auto p = linePair(s, lineA, lineB);
    const auto m = p ? meeting(*p) : std::nullopt;
    if (!m)
        return std::nullopt;
    return p->a0 + p->da * m->first;
}

std::optional<double> visibleAngle(const Sketch& s, EntityId lineA, EntityId lineB, double directionAngle)
{
    const auto p = linePair(s, lineA, lineB);
    const auto sides = p ? raySides(*p) : std::nullopt;
    if (!sides)
        return std::nullopt;
    // Turning one direction round adds half a turn.
    return std::abs(std::remainder(directionAngle + (sides->first * sides->second < 0 ? kPi : 0.0), 2 * kPi));
}

std::optional<double> directionAngleFor(const Sketch& s, EntityId lineA, EntityId lineB, double visible)
{
    const auto p = linePair(s, lineA, lineB);
    const auto sides = p ? raySides(*p) : std::nullopt;
    if (!sides)
        return std::nullopt;
    // The rays keep turning the way they turn now.
    const double raysNow = signedAngle(p->da * sides->first, p->db * sides->second);
    const double rays = raysNow < 0 ? -visible : visible;
    return std::remainder(rays + (sides->first * sides->second < 0 ? kPi : 0.0), 2 * kPi);
}

RectangleIds addRectangle(Sketch& sketch, Vec2 a, Vec2 b, EntityId reuseFirstCorner)
{
    RectangleIds ids;
    ids.corners[0] = reuseFirstCorner != kNoEntity && sketch.point(reuseFirstCorner) ? reuseFirstCorner : sketch.addPoint(a);
    ids.corners[1] = sketch.addPoint({b.x, a.y});
    ids.corners[2] = sketch.addPoint(b);
    ids.corners[3] = sketch.addPoint({a.x, b.y});
    for (int i = 0; i < 4; ++i)
        ids.edges[i] = sketch.addLine(ids.corners[i], ids.corners[(i + 1) % 4]);
    sketch.addConstraint({ConstraintKind::Horizontal, ids.edges[0]});
    sketch.addConstraint({ConstraintKind::Vertical, ids.edges[1]});
    sketch.addConstraint({ConstraintKind::Horizontal, ids.edges[2]});
    sketch.addConstraint({ConstraintKind::Vertical, ids.edges[3]});
    return ids;
}

// ---- JSON ----------------------------------------------------------------------

namespace {

const char* kindName(ConstraintKind k)
{
    switch (k) {
    case ConstraintKind::Coincident: return "Coincident";
    case ConstraintKind::Horizontal: return "Horizontal";
    case ConstraintKind::Vertical: return "Vertical";
    case ConstraintKind::Distance: return "Distance";
    case ConstraintKind::HorizontalDistance: return "HorizontalDistance";
    case ConstraintKind::VerticalDistance: return "VerticalDistance";
    case ConstraintKind::Diameter: return "Diameter";
    case ConstraintKind::Parallel: return "Parallel";
    case ConstraintKind::Perpendicular: return "Perpendicular";
    case ConstraintKind::Equal: return "Equal";
    case ConstraintKind::Tangent: return "Tangent";
    case ConstraintKind::Concentric: return "Concentric";
    case ConstraintKind::PointOnLine: return "PointOnLine";
    case ConstraintKind::Midpoint: return "Midpoint";
    case ConstraintKind::Radius: return "Radius";
    case ConstraintKind::PointOnCircle: return "PointOnCircle";
    case ConstraintKind::Symmetric: return "Symmetric";
    case ConstraintKind::Angle: return "Angle";
    }
    return "?";
}

std::optional<ConstraintKind> kindFromName(const std::string& s)
{
    for (auto k : {ConstraintKind::Coincident, ConstraintKind::Horizontal, ConstraintKind::Vertical, ConstraintKind::Distance,
                   ConstraintKind::HorizontalDistance, ConstraintKind::VerticalDistance, ConstraintKind::Diameter,
                   ConstraintKind::Parallel, ConstraintKind::Perpendicular, ConstraintKind::Equal, ConstraintKind::Tangent,
                   ConstraintKind::Concentric, ConstraintKind::PointOnLine, ConstraintKind::Midpoint,
                   ConstraintKind::Radius, ConstraintKind::PointOnCircle, ConstraintKind::Symmetric,
                   ConstraintKind::Angle})
        if (s == kindName(k))
            return k;
    return std::nullopt;
}

json vec3(const Vec3& v) { return json::array({v.x, v.y, v.z}); }

std::optional<Vec3> vec3From(const json& j)
{
    if (!j.is_array() || j.size() != 3)
        return std::nullopt;
    for (const auto& c : j)
        if (!c.is_number() || !std::isfinite(c.get<double>()))
            return std::nullopt;
    return Vec3{j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
}

bool finiteNumber(const json& j, const char* key)
{
    return j.contains(key) && j[key].is_number() && std::isfinite(j[key].get<double>());
}

// Entity ids are 32-bit; far below the wrap-around, so that allocating new
// ids after loading can never reach 0 (kNoEntity) or the origin again.
constexpr std::uint64_t kMaxEntityId = 1u << 30;

bool idField(const json& j, const char* key)
{
    return j.contains(key) && j[key].is_number_unsigned() && j[key].get<std::uint64_t>() <= kMaxEntityId;
}

// An optional boolean flag: absent is false; present with another type is invalid.
std::optional<bool> flagField(const json& j, const char* key)
{
    if (!j.contains(key))
        return false;
    if (!j[key].is_boolean())
        return std::nullopt;
    return j[key].get<bool>();
}

} // namespace

json Sketch::toJson() const
{
    json pts = json::array(), lns = json::array(), cls = json::array(), arcs = json::array(), cns = json::array();
    for (const auto& [id, p] : points_)
        pts.push_back({{"id", id}, {"x", p.position.x}, {"y", p.position.y}, {"fixed", p.fixed}});
    for (const auto& [id, l] : lines_)
        lns.push_back({{"id", id}, {"start", l.start}, {"end", l.end}, {"construction", l.construction}});
    for (const auto& [id, c] : circles_)
        cls.push_back({{"id", id}, {"center", c.center}, {"radius", c.radius}, {"construction", c.construction}});
    for (const auto& [id, a] : arcs_)
        arcs.push_back({{"id", id}, {"center", a.center}, {"start", a.start}, {"end", a.end}, {"construction", a.construction}});
    for (const auto& [id, c] : constraints_) {
        json entry{{"id", id}, {"type", kindName(c.kind)}, {"a", c.a}, {"b", c.b}, {"value", c.value}};
        if (c.c != kNoEntity)
            entry["c"] = c.c; // only constraints with a third entity (Symmetric) write it
        cns.push_back(std::move(entry));
    }
    json attachment = nullptr;
    if (attachment_)
        attachment = {{"body", attachment_->body.toString()},
                      {"feature", attachment_->feature.toString()},
                      {"faceHint", attachment_->faceHint},
                      {"normal", vec3(attachment_->faceNormal)},
                      {"centroid", vec3(attachment_->faceCentroid)},
                      {"area", attachment_->faceArea}};
    json out{{"id", id_.toString()},
            {"name", name_},
            {"attachment", attachment},
            {"visible", visible_},
            {"plane", {{"origin", vec3(plane_.origin)}, {"xAxis", vec3(plane_.xAxis)}, {"yAxis", vec3(plane_.yAxis)}}},
            {"hostBody", hostBody_ ? json(hostBody_->toString()) : json(nullptr)},
            {"nextId", nextId_},
            {"points", pts},
            {"lines", lns},
            {"circles", cls},
            {"arcs", arcs},
            {"constraints", cns}};
    if (datumPlane_)
        out["datumPlane"] = datumPlane_->toString();
    return out;
}

Result<Sketch> Sketch::fromJson(const json& j)
{
    auto bad = [](const std::string& why) {
        return Result<Sketch>::failure(ErrorCode::FileFormatError, "The file contains an invalid sketch.", "sketch: " + why);
    };
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string())
        return bad("missing id");
    const auto id = Uuid::parse(j["id"].get<std::string>());
    if (!id || id->isNil())
        return bad("invalid id");
    if (!j.contains("plane") || !j["plane"].is_object())
        return bad("missing plane");
    const auto origin = vec3From(j["plane"].value("origin", json()));
    const auto xAxis = vec3From(j["plane"].value("xAxis", json()));
    const auto yAxis = vec3From(j["plane"].value("yAxis", json()));
    if (!origin || !xAxis || !yAxis)
        return bad("invalid plane");
    if (std::abs(xAxis->length() - 1) > 1e-6 || std::abs(yAxis->length() - 1) > 1e-6 || std::abs(xAxis->dot(*yAxis)) > 1e-6)
        return bad("plane axes are not orthonormal");

    Sketch s(*id, Plane{*origin, *xAxis, *yAxis});
    if (j.contains("name") && j["name"].is_string())
        s.name_ = j["name"].get<std::string>();
    if (j.contains("visible") && j["visible"].is_boolean())
        s.visible_ = j["visible"].get<bool>();
    if (j.contains("hostBody") && j["hostBody"].is_string())
        s.hostBody_ = Uuid::parse(j["hostBody"].get<std::string>());
    if (j.contains("attachment") && j["attachment"].is_object()) {
        const json& a = j["attachment"];
        const auto body = a.contains("body") && a["body"].is_string() ? Uuid::parse(a["body"].get<std::string>()) : std::nullopt;
        const auto feature = a.contains("feature") && a["feature"].is_string() ? Uuid::parse(a["feature"].get<std::string>())
                                                                               : std::nullopt;
        const auto normal = vec3From(a.value("normal", json()));
        const auto centroid = vec3From(a.value("centroid", json()));
        if (!body || !feature || !normal || !centroid || !finiteNumber(a, "area") || !a.contains("faceHint")
            || !a["faceHint"].is_number_integer())
            return bad("invalid attachment");
        s.attachment_ = Attachment{*body, *feature, a["faceHint"].get<int>(), *normal, *centroid, a["area"].get<double>()};
    }
    if (j.contains("datumPlane")) {
        const auto datum = j["datumPlane"].is_string() ? Uuid::parse(j["datumPlane"].get<std::string>()) : std::nullopt;
        if (!datum || datum->isNil())
            return bad("invalid datumPlane");
        s.datumPlane_ = *datum;
    }
    if (!idField(j, "nextId"))
        return bad("missing nextId");
    s.nextId_ = j["nextId"].get<EntityId>();

    for (const char* key : {"points", "lines", "circles", "constraints"})
        if (!j.contains(key) || !j[key].is_array())
            return bad(std::string("missing ") + key);

    s.points_.clear();
    auto takeId = [&](const json& e, EntityId& out) {
        if (!idField(e, "id"))
            return false;
        out = e["id"].get<EntityId>();
        return out != kNoEntity && out < s.nextId_ && !s.exists(out);
    };
    for (const auto& e : j["points"]) {
        EntityId eid{};
        const auto fixed = e.is_object() ? flagField(e, "fixed") : std::nullopt;
        if (!e.is_object() || !takeId(e, eid) || !finiteNumber(e, "x") || !finiteNumber(e, "y") || !fixed)
            return bad("invalid point");
        s.points_[eid] = SketchPoint{{e["x"].get<double>(), e["y"].get<double>()}, *fixed};
    }
    if (!s.points_.contains(kOriginId))
        return bad("missing origin point");
    for (const auto& e : j["lines"]) {
        EntityId eid{};
        const auto construction = e.is_object() ? flagField(e, "construction") : std::nullopt;
        if (!e.is_object() || !takeId(e, eid) || !idField(e, "start") || !idField(e, "end") || !construction)
            return bad("invalid line");
        SketchLine l{e["start"].get<EntityId>(), e["end"].get<EntityId>(), *construction};
        if (!s.points_.contains(l.start) || !s.points_.contains(l.end) || l.start == l.end)
            return bad("line references missing points");
        s.lines_[eid] = l;
    }
    for (const auto& e : j["circles"]) {
        EntityId eid{};
        const auto construction = e.is_object() ? flagField(e, "construction") : std::nullopt;
        if (!e.is_object() || !takeId(e, eid) || !idField(e, "center") || !finiteNumber(e, "radius") || !construction)
            return bad("invalid circle");
        SketchCircle c{e["center"].get<EntityId>(), e["radius"].get<double>(), *construction};
        if (!s.points_.contains(c.center) || !(c.radius > 0))
            return bad("circle references missing center or has bad radius");
        s.circles_[eid] = c;
    }
    // Arcs arrived after the first files were written: optional.
    if (j.contains("arcs")) {
        if (!j["arcs"].is_array())
            return bad("invalid arcs");
        for (const auto& e : j["arcs"]) {
            EntityId eid{};
            const auto construction = e.is_object() ? flagField(e, "construction") : std::nullopt;
            if (!e.is_object() || !takeId(e, eid) || !idField(e, "center") || !idField(e, "start") || !idField(e, "end")
                || !construction)
                return bad("invalid arc");
            SketchArc a{e["center"].get<EntityId>(), e["start"].get<EntityId>(), e["end"].get<EntityId>(), *construction};
            if (!s.points_.contains(a.center) || !s.points_.contains(a.start) || !s.points_.contains(a.end)
                || a.center == a.start || a.center == a.end || a.start == a.end)
                return bad("arc references missing or repeated points");
            s.arcs_[eid] = a;
        }
    }
    for (const auto& e : j["constraints"]) {
        EntityId eid{};
        if (!e.is_object() || !takeId(e, eid) || !e.contains("type") || !e["type"].is_string() || !idField(e, "a")
            || !idField(e, "b") || !finiteNumber(e, "value"))
            return bad("invalid constraint");
        const auto kind = kindFromName(e["type"].get<std::string>());
        if (!kind)
            return Result<Sketch>::failure(ErrorCode::FileVersionUnsupported,
                                           "This sketch uses a constraint this version of OpenShape does not support.",
                                           "unknown constraint type");
        if (e.contains("c") && !idField(e, "c"))
            return bad("invalid constraint");
        const SketchConstraint c{*kind, e["a"].get<EntityId>(), e["b"].get<EntityId>(), e["value"].get<double>(),
                                 e.contains("c") ? e["c"].get<EntityId>() : kNoEntity};
        if (!s.isValid(c))
            return bad("constraint references invalid entities");
        s.constraints_[eid] = c;
    }
    return Result<Sketch>::success(std::move(s));
}

} // namespace os::sketch
