// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bridge between the OpenShape sketch model and PlaneGCS. This is the only
// file that includes solver headers.
#include "sketch/Sketch.h"

#include "core/Log.h"
#include "core/Timer.h"

#include <planegcs/GCS.h>

#include <deque>
#include <map>

namespace os::sketch {

namespace {

// Owns parameter storage (stable addresses) and the solver system for one solve.
class SolverRun {
public:
    explicit SolverRun(const Sketch& sketch) : sketch_(sketch) {}

    void build()
    {
        for (const auto& [id, p] : sketch_.points()) {
            double* x = param(p.position.x);
            double* y = param(p.position.y);
            points_[id] = GCS::Point(x, y);
            if (!p.fixed) {
                unknowns_.push_back(x);
                unknowns_.push_back(y);
            }
        }
        for (const auto& [id, l] : sketch_.lines()) {
            GCS::Line line;
            line.p1 = points_.at(l.start);
            line.p2 = points_.at(l.end);
            lines_[id] = line;
        }
        for (const auto& [id, c] : sketch_.circles()) {
            GCS::Circle circle;
            circle.center = points_.at(c.center);
            circle.rad = param(c.radius);
            unknowns_.push_back(circle.rad);
            circles_[id] = circle;
        }
        for (const auto& [id, a] : sketch_.arcs()) {
            // Counterclockwise from start to end; ArcRules ties the end points
            // to center + radius at the two angles.
            const Vec2 c = sketch_.point(a.center)->position;
            const Vec2 s = sketch_.point(a.start)->position;
            const Vec2 e = sketch_.point(a.end)->position;
            const double a0 = std::atan2(s.y - c.y, s.x - c.x);
            double a1 = std::atan2(e.y - c.y, e.x - c.x);
            if (a1 <= a0)
                a1 += 2 * kPi;
            GCS::Arc arc;
            arc.center = points_.at(a.center);
            arc.start = points_.at(a.start);
            arc.end = points_.at(a.end);
            arc.rad = param((s - c).length());
            arc.startAngle = param(a0);
            arc.endAngle = param(a1);
            unknowns_.push_back(arc.rad);
            unknowns_.push_back(arc.startAngle);
            unknowns_.push_back(arc.endAngle);
            arcs_[id] = arc;
            system_.addConstraintArcRules(arcs_[id], 0); // tag 0: structural, never reported as a conflict
        }
        for (const auto& [id, c] : sketch_.constraints()) {
            const int tag = static_cast<int>(id);
            switch (c.kind) {
            case ConstraintKind::Coincident:
                system_.addConstraintP2PCoincident(points_.at(c.a), points_.at(c.b), tag);
                break;
            case ConstraintKind::Horizontal:
                system_.addConstraintHorizontal(lines_.at(c.a), tag);
                break;
            case ConstraintKind::Vertical:
                system_.addConstraintVertical(lines_.at(c.a), tag);
                break;
            case ConstraintKind::Distance:
                system_.addConstraintP2PDistance(points_.at(c.a), points_.at(c.b), param(c.value), tag);
                break;
            case ConstraintKind::HorizontalDistance:
                // error = b.x - a.x - value
                system_.addConstraintDifference(points_.at(c.a).x, points_.at(c.b).x, param(c.value), tag);
                break;
            case ConstraintKind::VerticalDistance:
                system_.addConstraintDifference(points_.at(c.a).y, points_.at(c.b).y, param(c.value), tag);
                break;
            case ConstraintKind::Diameter:
                system_.addConstraintCircleDiameter(circles_.at(c.a), param(c.value), tag);
                break;
            case ConstraintKind::Parallel:
                system_.addConstraintParallel(lines_.at(c.a), lines_.at(c.b), tag);
                break;
            case ConstraintKind::Perpendicular:
                system_.addConstraintPerpendicular(lines_.at(c.a), lines_.at(c.b), tag);
                break;
            case ConstraintKind::Equal:
                if (lines_.contains(c.a))
                    system_.addConstraintEqualLength(lines_.at(c.a), lines_.at(c.b), tag);
                else
                    system_.addConstraintEqualRadius(round(c.a), round(c.b), tag);
                break;
            case ConstraintKind::Radius:
                system_.addConstraintArcRadius(arcs_.at(c.a), param(c.value), tag);
                break;
            case ConstraintKind::Tangent:
                if (lines_.contains(c.a)) {
                    // Keep the circle on the side of the line it is on now.
                    const SketchLine& l = sketch_.lines().at(c.a);
                    const Vec2 p = sketch_.point(l.start)->position;
                    const Vec2 d = sketch_.point(l.end)->position - p;
                    const Vec2 q = roundCenter(c.b) - p;
                    system_.addConstraintTangent(lines_.at(c.a), round(c.b), d.x * q.y - d.y * q.x > 0, tag);
                } else {
                    system_.addConstraintTangent(round(c.a), round(c.b), tag);
                }
                break;
            case ConstraintKind::Concentric:
                system_.addConstraintP2PCoincident(round(c.a).center, round(c.b).center, tag);
                break;
            case ConstraintKind::PointOnLine:
                system_.addConstraintPointOnLine(points_.at(c.a), lines_.at(c.b), tag);
                break;
            case ConstraintKind::Midpoint:
                // The line's endpoints are symmetric about the point.
                system_.addConstraintP2PSymmetric(lines_.at(c.b).p1, lines_.at(c.b).p2, points_.at(c.a), tag);
                break;
            }
        }
    }

    // Circles and arcs as solver circles (an arc is a circle with angles).
    GCS::Circle& round(EntityId id)
    {
        if (auto it = circles_.find(id); it != circles_.end())
            return it->second;
        return arcs_.at(id);
    }
    Vec2 roundCenter(EntityId id) const
    {
        if (const SketchCircle* c = sketch_.circle(id))
            return sketch_.point(c->center)->position;
        return sketch_.point(sketch_.arc(id)->center)->position;
    }

    // Adds a soft constraint pulling a point to a target (interactive drag).
    void addDragTarget(EntityId pointId, Vec2 target)
    {
        auto it = points_.find(pointId);
        if (it == points_.end())
            return;
        dragTarget_ = GCS::Point(param(target.x), param(target.y));
        system_.addConstraintP2PCoincident(dragTarget_, it->second, GCS::DefaultTemporaryConstraint);
    }

    SolveReport solve()
    {
        SolveReport report;
        system_.declareUnknowns(unknowns_);
        GCS::VEC_pD driven;
        system_.declareDrivenParams(driven);
        system_.initSolution(GCS::DogLeg);

        GCS::VEC_I conflicting, redundant;
        system_.getConflicting(conflicting);
        system_.getRedundant(redundant);
        for (int tag : conflicting)
            if (tag > 0)
                report.conflicting.push_back(static_cast<EntityId>(tag));
        for (int tag : redundant)
            if (tag > 0)
                report.redundant.push_back(static_cast<EntityId>(tag));
        report.degreesOfFreedom = system_.dofsNumber();

        const GCS::SolveStatus status = system_.solve(GCS::DogLeg);
        if (status == GCS::SolveStatus::Success || status == GCS::SolveStatus::Converged) {
            system_.applySolution();
        } else {
            system_.undoSolution();
            report.ok = false;
        }
        if (!report.conflicting.empty()) {
            report.ok = false;
            report.message = "These constraints contradict each other.";
        } else if (!report.ok) {
            report.message = "The sketch cannot satisfy these constraints.";
        }
        return report;
    }

    // Writes solved parameters back into a sketch.
    void writeBack(Sketch& sketch) const
    {
        for (const auto& [id, p] : points_)
            if (SketchPoint* sp = sketch.point(id))
                sp->position = {*p.x, *p.y};
        for (const auto& [id, c] : circles_)
            if (SketchCircle* sc = sketch.circle(id))
                sc->radius = *c.rad;
    }

private:
    double* param(double value)
    {
        storage_.push_back(value);
        return &storage_.back();
    }

    const Sketch& sketch_;
    std::deque<double> storage_; // deque: stable addresses on push_back
    GCS::VEC_pD unknowns_;
    std::map<EntityId, GCS::Point> points_;
    std::map<EntityId, GCS::Line> lines_;
    std::map<EntityId, GCS::Circle> circles_;
    std::map<EntityId, GCS::Arc> arcs_;
    GCS::Point dragTarget_;
    GCS::System system_;
};

SolveReport run(Sketch& sketch, std::optional<std::pair<EntityId, Vec2>> drag)
{
    ScopedTimer timer("sketch solve");
    SolverRun solver(sketch);
    solver.build();
    if (drag)
        solver.addDragTarget(drag->first, drag->second);
    SolveReport report = solver.solve();
    if (report.ok)
        solver.writeBack(sketch);
    else
        OS_LOG(Info, Constraint) << "sketch " << sketch.id().toString() << " did not solve: " << report.message
                                 << " (conflicting " << report.conflicting.size() << ")";
    OS_LOG(Debug, Constraint) << "sketch solved, dof=" << report.degreesOfFreedom;
    sketch.setSolveReport(report);
    return report;
}

} // namespace

SolveReport solve(Sketch& sketch)
{
    return run(sketch, std::nullopt);
}

SolveReport solveDragging(Sketch& sketch, EntityId pointId, Vec2 target)
{
    return run(sketch, std::make_pair(pointId, target));
}

} // namespace os::sketch
