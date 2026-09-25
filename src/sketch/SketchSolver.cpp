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
            }
        }
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
