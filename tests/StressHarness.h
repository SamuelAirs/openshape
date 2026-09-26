// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// Seeded random modeling sessions for the robustness tests: real edits
// through the InteractionController and the same commands it pushes, plus a
// comparable summary of the whole document (per body: volume, bounding box,
// face count, history with parameters and status; per sketch: plane and
// entity counts).

#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "geometry/TopoSignature.h"
#include "interaction/InteractionController.h"
#include "sketch/Sketch.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace os::test {

struct FeatureSummary {
    Uuid id;
    doc::FeatureKind kind = doc::FeatureKind::Box;
    bool suppressed = false;
    doc::FeatureStatus status = doc::FeatureStatus::NotComputed;
    std::string params; // writeParams() as JSON text: exact, parameters are only ever set, never computed
};

struct BodySummary {
    Uuid id;
    std::string name;
    bool visible = true;
    double volume = 0;
    geom::BoundingBox box;
    int faces = 0;
    int solids = 0;
    std::vector<FeatureSummary> features;
};

struct SketchSummary {
    Uuid id;
    std::string name;
    bool visible = true;
    sketch::Plane plane;
    std::size_t points = 0, lines = 0, circles = 0, arcs = 0, constraints = 0;
};

struct Snapshot {
    std::vector<BodySummary> bodies;
    std::vector<SketchSummary> sketches;
};

// Volume, box and counts per shape, cached by (body, shape revision): a
// revision is never reused, and most steps change one body only.
class MetricsCache {
public:
    struct Metrics {
        double volume = 0;
        geom::BoundingBox box;
        int faces = 0;
        int solids = 0;
    };

    const Metrics& of(const doc::Body& body)
    {
        const std::string key = body.id().toString() + "#" + std::to_string(body.shapeRevision());
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;
        Metrics m;
        const geom::Shape& s = body.shape();
        if (!s.isNull()) {
            m.volume = geom::volume(s);
            m.box = geom::approximateBoundingBox(s);
            m.faces = s.faceCount();
            m.solids = s.solidCount();
        }
        return cache_.emplace(key, m).first->second;
    }

private:
    std::unordered_map<std::string, Metrics> cache_;
};

inline Snapshot snapshotOf(const doc::Document& document, MetricsCache& cache)
{
    Snapshot out;
    for (const auto& body : document.bodies()) {
        BodySummary b;
        b.id = body->id();
        b.name = body->name();
        b.visible = body->isVisible();
        const auto& m = cache.of(*body);
        b.volume = m.volume;
        b.box = m.box;
        b.faces = m.faces;
        b.solids = m.solids;
        for (std::size_t i = 0; i < body->features().size(); ++i) {
            const doc::Feature& f = *body->features()[i];
            nlohmann::json params = nlohmann::json::object();
            f.writeParams(params);
            b.features.push_back({f.id(), f.kind(), f.isSuppressed(), body->state(int(i)).status, params.dump()});
        }
        out.bodies.push_back(std::move(b));
    }
    for (const auto& s : document.sketches())
        out.sketches.push_back({s->id(), s->name(), s->isVisible(), s->plane(), s->points().size(), s->lines().size(),
                                s->circles().size(), s->arcs().size(), s->constraints().size()});
    return out;
}

inline std::string statusName(doc::FeatureStatus s)
{
    switch (s) {
    case doc::FeatureStatus::NotComputed: return "NotComputed";
    case doc::FeatureStatus::Ok: return "Ok";
    case doc::FeatureStatus::Failed: return "Failed";
    case doc::FeatureStatus::Suppressed: return "Suppressed";
    }
    return "?";
}

// The document's bodies and their steps with status, for failure messages.
inline std::string describe(const doc::Document& document)
{
    std::string out;
    for (const auto& body : document.bodies()) {
        out += "  " + body->name() + (body->isVisible() ? "" : " (hidden)") + ": "
             + std::to_string(body->shape().isNull() ? 0 : body->shape().faceCount()) + " faces\n";
        for (std::size_t i = 0; i < body->features().size(); ++i) {
            const doc::Feature& f = *body->features()[i];
            nlohmann::json params = nlohmann::json::object();
            f.writeParams(params);
            std::string text = params.dump();
            if (text.size() > 160)
                text = text.substr(0, 160) + "...";
            out += "    " + std::string(doc::toString(f.kind())) + (f.isSuppressed() ? " (suppressed)" : "") + " "
                 + statusName(body->state(int(i)).status) + " " + body->state(int(i)).developerMessage + " " + text + "\n";
        }
    }
    return out;
}

// Equal up to floating-point noise in derived geometry; exact for identity,
// history and parameters.
inline ::testing::AssertionResult sameState(const Snapshot& a, const Snapshot& b)
{
    auto fail = [](const std::string& what) { return ::testing::AssertionFailure() << what; };
    auto near = [](double x, double y, double relative, double absolute) {
        return std::abs(x - y) <= absolute + relative * std::max(std::abs(x), std::abs(y));
    };
    auto nearVec = [&](const Vec3& x, const Vec3& y, double tolerance) {
        return near(x.x, y.x, 0, tolerance) && near(x.y, y.y, 0, tolerance) && near(x.z, y.z, 0, tolerance);
    };
    if (a.bodies.size() != b.bodies.size())
        return fail("body count " + std::to_string(a.bodies.size()) + " vs " + std::to_string(b.bodies.size()));
    for (std::size_t i = 0; i < a.bodies.size(); ++i) {
        const BodySummary& x = a.bodies[i];
        const BodySummary& y = b.bodies[i];
        const std::string where = "body " + std::to_string(i) + " '" + x.name + "': ";
        if (x.id != y.id || x.name != y.name || x.visible != y.visible)
            return fail(where + "identity, name or visibility differs");
        if (x.features.size() != y.features.size())
            return fail(where + "feature count " + std::to_string(x.features.size()) + " vs "
                        + std::to_string(y.features.size()));
        for (std::size_t k = 0; k < x.features.size(); ++k) {
            const FeatureSummary& fx = x.features[k];
            const FeatureSummary& fy = y.features[k];
            const std::string at = where + "feature " + std::to_string(k) + " (" + std::string(doc::toString(fx.kind)) + "): ";
            if (fx.id != fy.id || fx.kind != fy.kind)
                return fail(at + "identity differs");
            if (fx.suppressed != fy.suppressed)
                return fail(at + "suppression differs");
            if (fx.status != fy.status)
                return fail(at + "status " + statusName(fx.status) + " vs " + statusName(fy.status));
            if (fx.params != fy.params)
                return fail(at + "parameters differ:\n  " + fx.params + "\n  " + fy.params);
        }
        if (x.faces != y.faces || x.solids != y.solids)
            return fail(where + "faces " + std::to_string(x.faces) + " vs " + std::to_string(y.faces) + ", solids "
                        + std::to_string(x.solids) + " vs " + std::to_string(y.solids));
        if (!near(x.volume, y.volume, 1e-7, 1e-7))
            return fail(where + "volume " + std::to_string(x.volume) + " vs " + std::to_string(y.volume));
        if (x.box.valid != y.box.valid || (x.box.valid && (!nearVec(x.box.min, y.box.min, 1e-5) || !nearVec(x.box.max, y.box.max, 1e-5)))) {
            char text[256];
            std::snprintf(text, sizeof text, "(%.9g %.9g %.9g)-(%.9g %.9g %.9g) vs (%.9g %.9g %.9g)-(%.9g %.9g %.9g)", x.box.min.x,
                          x.box.min.y, x.box.min.z, x.box.max.x, x.box.max.y, x.box.max.z, y.box.min.x, y.box.min.y, y.box.min.z,
                          y.box.max.x, y.box.max.y, y.box.max.z);
            return fail(where + "bounding box differs: " + text);
        }
    }
    if (a.sketches.size() != b.sketches.size())
        return fail("sketch count " + std::to_string(a.sketches.size()) + " vs " + std::to_string(b.sketches.size()));
    for (std::size_t i = 0; i < a.sketches.size(); ++i) {
        const SketchSummary& x = a.sketches[i];
        const SketchSummary& y = b.sketches[i];
        const std::string where = "sketch " + std::to_string(i) + " '" + x.name + "': ";
        if (x.id != y.id || x.name != y.name || x.visible != y.visible)
            return fail(where + "identity, name or visibility differs");
        if (x.points != y.points || x.lines != y.lines || x.circles != y.circles || x.arcs != y.arcs
            || x.constraints != y.constraints)
            return fail(where + "entity counts differ");
        if (!nearVec(x.plane.origin, y.plane.origin, 1e-6) || !nearVec(x.plane.xAxis, y.plane.xAxis, 1e-9)
            || !nearVec(x.plane.yAxis, y.plane.yAxis, 1e-9))
            return fail(where + "plane differs");
    }
    return ::testing::AssertionSuccess();
}

// A random but realistic modeling session. Every call to edit() attempts one
// user action; edits the kernel or the document refuse are skipped (they must
// leave the document unchanged, which the callers check through snapshots).
class RandomModeler {
public:
    RandomModeler(doc::Document& document, cmd::UndoStack& stack, interact::InteractionController& controller,
                  unsigned seed)
        : document_(document), stack_(stack), controller_(controller), rng_(seed)
    {
    }

    // One attempted edit. Returns true when it added exactly one undo step.
    // `what` receives a short description (for failure messages).
    bool edit(std::string* what = nullptr)
    {
        const std::size_t before = stack_.index();
        const std::size_t sizeBefore = stack_.size();
        std::string label;
        attempt(label);
        if (what)
            *what = label;
        const std::size_t after = stack_.index();
        EXPECT_TRUE(after == before || after == before + 1) << label << " pushed " << (after - before) << " steps";
        if (after == before) {
            EXPECT_EQ(stack_.size(), sizeBefore) << label << " was refused but changed the undo stack";
        }
        return after == before + 1;
    }

    std::mt19937& rng() { return rng_; }

private:
    double uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng_); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    bool chance(double p) { return uniform(0, 1) < p; }
    template <typename T>
    const T& pickFrom(const std::vector<T>& v) { return v[std::size_t(integer(0, int(v.size()) - 1))]; }

    bool push(std::unique_ptr<cmd::Command> command)
    {
        const Status status = stack_.push(std::move(command), document_);
        controller_.documentChanged();
        return status.ok();
    }

    std::vector<const doc::Body*> bodies(int maxFaces = 1 << 30) const
    {
        std::vector<const doc::Body*> out;
        for (const auto& b : document_.bodies())
            if (!b->shape().isNull() && b->shape().faceCount() <= maxFaces)
                out.push_back(b.get());
        return out;
    }

    const doc::Body* randomBody(int maxFaces = 1 << 30)
    {
        const auto list = bodies(maxFaces);
        return list.empty() ? nullptr : pickFrom(list);
    }

    std::vector<int> planarFaces(const geom::Shape& s) const
    {
        std::vector<int> out;
        for (int i = 0; i < s.faceCount(); ++i)
            if (const auto info = geom::faceInfo(s, i); info && info->isPlanar())
                out.push_back(i);
        return out;
    }

    std::vector<std::pair<const doc::Body*, const doc::Feature*>> nonBaseFeatures() const
    {
        std::vector<std::pair<const doc::Body*, const doc::Feature*>> out;
        for (const auto& b : document_.bodies())
            for (std::size_t i = 1; i < b->features().size(); ++i)
                out.emplace_back(b.get(), b->features()[i].get());
        return out;
    }

    static std::string number(double v)
    {
        char text[64];
        std::snprintf(text, sizeof text, "%.3f", v);
        return text;
    }

    void attempt(std::string& label)
    {
        // Weighted choice of an action; actions that do not apply fall through to a box.
        struct Action {
            const char* name;
            int weight;
            std::function<void()> run;
        };
        const std::vector<Action> actions{
            {"box", document_.bodies().size() < 5 ? 3 : 0, [&] { (void)controller_.createBox(uniform(8, 25)); }},
            {"pushpull", 5, [&] { pushPull(); }},
            {"fillet", 3, [&] { edgeTreatment(true); }},
            {"chamfer", 2, [&] { edgeTreatment(false); }},
            {"shell", 1, [&] { shell(); }},
            {"sketch", 2, [&] { newSketch(); }},
            {"sketchEdit", 1, [&] { editSketch(); }},
            {"extrude", 4, [&] { extrude(); }},
            {"move", 2, [&] { move(false); }},
            {"rotate", 1, [&] { move(true); }},
            {"mirror", 1, [&] { mirror(); }},
            {"pattern", 1, [&] { pattern(); }},
            {"combine", 1, [&] { combine(); }},
            {"parameter", 4, [&] { editParameter(); }},
            {"suppress", 2, [&] { toggleSuppressed(); }},
            {"deleteStep", 1, [&] { deleteStep(); }},
            {"deleteBody", document_.bodies().size() >= 3 ? 1 : 0, [&] { deleteBody(); }},
            {"deleteFaces", 1, [&] { deleteFaces(); }},
            {"offsetFace", 1, [&] { offsetRoundFace(); }},
            {"hole", 1, [&] { hole(); }},
            {"visibility", 1, [&] { toggleVisible(); }},
        };
        int total = 0;
        for (const auto& a : actions)
            total += a.weight;
        int roll = integer(0, total - 1);
        for (const auto& a : actions) {
            if (roll < a.weight) {
                label = a.name;
                if (document_.bodies().empty() && std::string(a.name) != "sketch" && std::string(a.name) != "extrude") {
                    label = "box";
                    (void)controller_.createBox(uniform(8, 25));
                    return;
                }
                a.run();
                return;
            }
            roll -= a.weight;
        }
    }

    // ---- Actions ----

    void pushPull()
    {
        const doc::Body* body = randomBody(200);
        if (!body)
            return;
        const auto faces = planarFaces(body->shape());
        if (faces.empty())
            return;
        const int f = pickFrom(faces);
        auto feature = std::make_unique<doc::PushPullFeature>();
        feature->face = {f, *geom::captureFaceSignature(body->shape(), f)};
        double d = uniform(-4, 6);
        if (std::abs(d) < 0.5)
            d = 1.5;
        feature->distance = d;
        feature->keepEdges = chance(0.7);
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void edgeTreatment(bool fillet)
    {
        const doc::Body* body = randomBody(200);
        if (!body)
            return;
        const geom::Shape& s = body->shape();
        std::vector<int> lines;
        for (int e = 0; e < s.edgeCount(); ++e)
            if (const auto info = geom::edgeInfo(s, e); info && info->kind == geom::CurveKind::Line)
                lines.push_back(e);
        if (lines.empty())
            return;
        std::shuffle(lines.begin(), lines.end(), rng_);
        lines.resize(std::min<std::size_t>(lines.size(), std::size_t(integer(1, 3))));
        std::unique_ptr<doc::EdgeTreatmentFeature> feature;
        if (fillet)
            feature = std::make_unique<doc::FilletFeature>();
        else
            feature = std::make_unique<doc::ChamferFeature>();
        for (int e : lines)
            feature->edges.push_back({e, *geom::captureEdgeSignature(s, e)});
        feature->size = uniform(0.3, 2.0);
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void shell()
    {
        const doc::Body* body = randomBody(60);
        if (!body)
            return;
        const auto faces = planarFaces(body->shape());
        if (faces.empty())
            return;
        const int f = pickFrom(faces);
        auto feature = std::make_unique<doc::ShellFeature>();
        feature->faces = {{f, *geom::captureFaceSignature(body->shape(), f)}};
        feature->thickness = uniform(0.8, 2.0);
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void newSketch()
    {
        sketch::Sketch s(Uuid::generate(), sketch::Plane::xy());
        s.setName(document_.nextSketchName());
        Vec2 center{uniform(-30, 60), uniform(-30, 30)};
        const doc::Body* body = chance(0.7) ? randomBody(200) : nullptr;
        if (body) {
            // On a flat face, like InteractionController::startSketch.
            const auto faces = planarFaces(body->shape());
            if (faces.empty())
                return;
            const int f = pickFrom(faces);
            const auto info = geom::faceInfo(body->shape(), f);
            const Vec3 n = info->normal.normalized();
            s.setPlane(sketch::Plane::fromNormal(n * n.dot(info->centroid), n));
            s.setHostBody(body->id());
            for (int i = int(body->features().size()) - 1; i >= 0; --i) {
                const auto status = body->state(i).status;
                if (status == doc::FeatureStatus::Ok || status == doc::FeatureStatus::Suppressed) {
                    s.setAttachment(doc::makeAttachment(*body, body->features()[std::size_t(i)]->id(), f));
                    break;
                }
            }
            const Vec3 onFace = geom::pointOnFace(body->shape(), f, info->centroid).value_or(info->centroid);
            center = s.plane().toLocal(onFace);
        }
        if (chance(0.5)) {
            const double w = uniform(2, 8), h = uniform(2, 8);
            sketch::addRectangle(s, center - Vec2{w / 2, h / 2}, center + Vec2{w / 2, h / 2});
        } else {
            s.addCircle(s.addPoint(center), uniform(1, 4));
        }
        if (!sketch::solve(s).ok)
            return;
        (void)push(std::make_unique<cmd::CreateSketchCommand>(std::move(s)));
    }

    void editSketch()
    {
        if (document_.sketches().empty())
            return;
        sketch::Sketch copy = *pickFrom(document_.sketches());
        if (!copy.circles().empty() && chance(0.6)) {
            std::vector<sketch::EntityId> ids;
            for (const auto& [id, c] : copy.circles())
                ids.push_back(id);
            copy.circle(pickFrom(ids))->radius *= uniform(0.7, 1.3);
        } else {
            // Another circle next to the existing geometry.
            Vec2 near{0, 0};
            for (const auto& [id, p] : copy.points())
                near = p.position;
            copy.addCircle(copy.addPoint(near + Vec2{uniform(-6, 6), uniform(-6, 6)}), uniform(0.8, 3));
        }
        if (!sketch::solve(copy).ok)
            return;
        (void)push(std::make_unique<cmd::EditSketchCommand>(std::move(copy), "Edit sketch"));
    }

    void extrude()
    {
        if (document_.sketches().empty())
            return;
        const sketch::Sketch& s = *pickFrom(document_.sketches());
        auto regions = doc::sketchRegions(s);
        if (!regions || regions.value().empty())
            return;
        const auto& region = pickFrom(regions.value());
        auto feature = std::make_unique<doc::ExtrudeFeature>();
        feature->sketchId = s.id();
        feature->profiles = {doc::makeProfileRef(region, s)};
        const doc::Body* host = s.hostBody() ? document_.body(*s.hostBody()) : nullptr;
        const int mode = host ? integer(0, 2) : 0;
        feature->mode = mode == 0 ? doc::ExtrudeMode::NewBody : mode == 1 ? doc::ExtrudeMode::Join : doc::ExtrudeMode::Cut;
        feature->distance = feature->mode == doc::ExtrudeMode::Cut ? -uniform(1, 8) : uniform(2, 10);
        feature->throughAll = feature->mode == doc::ExtrudeMode::Cut && chance(0.3);
        feature->symmetric = chance(0.15);
        if (feature->mode == doc::ExtrudeMode::NewBody) {
            if (document_.bodies().size() >= 6)
                return;
            (void)push(std::make_unique<cmd::CreateBodyCommand>(document_.nextBodyName(), std::move(feature)));
        } else {
            (void)push(std::make_unique<cmd::AddFeatureCommand>(host->id(), std::move(feature)));
        }
    }

    void move(bool rotate)
    {
        const doc::Body* body = randomBody();
        if (!body)
            return;
        auto feature = std::make_unique<doc::MoveFeature>();
        feature->translation = {uniform(-10, 10), uniform(-10, 10), rotate ? 0.0 : uniform(-5, 5)};
        if (rotate) {
            feature->setName("Rotate");
            feature->rotates = true;
            feature->rotationCenter = geom::approximateBoundingBox(body->shape()).center();
            const int axis = integer(0, 2);
            feature->rotationAxis = {axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
            feature->rotationAngle = integer(1, 23) * 15 * kPi / 180;
        }
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void mirror()
    {
        const doc::Body* body = randomBody(80);
        if (!body)
            return;
        const auto box = geom::approximateBoundingBox(body->shape());
        auto feature = std::make_unique<doc::MirrorFeature>();
        const int axis = integer(0, 2);
        feature->planeNormal = {axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
        // Across a side of the part (the image touches it) or through its middle.
        feature->planeOrigin = chance(0.6) ? box.max : box.center();
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void pattern()
    {
        const doc::Body* body = randomBody(60);
        if (!body)
            return;
        const auto box = geom::approximateBoundingBox(body->shape());
        auto feature = std::make_unique<doc::PatternFeature>();
        if (chance(0.7)) {
            const int axis = integer(0, 1);
            feature->layout = doc::PatternFeature::Layout::Linear;
            feature->count = integer(2, 3);
            feature->direction = {axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, 0.0};
            const double extent = axis == 0 ? box.size().x : box.size().y;
            // Overlapping, touching or apart.
            feature->spacing = std::max(1.0, extent + uniform(-4, 6));
        } else {
            feature->layout = doc::PatternFeature::Layout::Circular;
            feature->count = integer(2, 4);
            feature->axisOrigin = box.center() + Vec3{box.size().x * 0.75, 0, 0};
            feature->axis = {0, 0, 1};
            feature->angle = 2 * kPi;
        }
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void combine()
    {
        std::vector<const doc::Body*> visible;
        for (const doc::Body* b : bodies(150))
            if (b->isVisible())
                visible.push_back(b);
        if (visible.size() < 2)
            return;
        std::shuffle(visible.begin(), visible.end(), rng_);
        const doc::CombineMode mode = std::array{doc::CombineMode::Union, doc::CombineMode::Subtract,
                                                 doc::CombineMode::Intersect}[std::size_t(integer(0, 2))];
        if (!controller_.selectBody(visible[0]->id(), false) || !controller_.selectBody(visible[1]->id(), true))
            return;
        (void)controller_.combineSelectedBodies(mode);
        (void)controller_.keyPress(interact::Key::Escape);
        (void)controller_.keyPress(interact::Key::Escape);
    }

    void editParameter()
    {
        std::vector<std::pair<const doc::Feature*, doc::ParameterInfo>> params;
        for (const auto& b : document_.bodies())
            for (const auto& f : b->features())
                for (const auto& p : f->parameters())
                    params.emplace_back(f.get(), p);
        if (params.empty())
            return;
        const auto& [feature, p] = pickFrom(params);
        std::string text;
        switch (p.kind) {
        case doc::ParameterKind::Length: {
            double v = p.value * uniform(0.7, 1.3);
            if (std::abs(v) < 0.2)
                v = v < 0 ? -0.5 : 0.5;
            text = number(v);
            break;
        }
        case doc::ParameterKind::Angle: text = number(p.value * 180 / kPi * uniform(0.5, 1.0)); break;
        case doc::ParameterKind::Count: text = std::to_string(std::max(1, int(std::lround(p.value)) + integer(-1, 1))); break;
        }
        (void)controller_.setFeatureParameter(feature->id(), p.key, text);
    }

    void toggleSuppressed()
    {
        const auto list = nonBaseFeatures();
        if (list.empty())
            return;
        const auto& [body, feature] = pickFrom(list);
        (void)controller_.setFeatureSuppressed(feature->id(), !feature->isSuppressed());
    }

    void deleteStep()
    {
        const auto list = nonBaseFeatures();
        if (list.empty())
            return;
        (void)controller_.deleteFeature(pickFrom(list).second->id());
    }

    void deleteBody()
    {
        const doc::Body* body = randomBody();
        if (body)
            (void)controller_.deleteBody(body->id());
    }

    void deleteFaces()
    {
        // Remove a rounded or bevelled face (a fillet, chamfer or hole wall).
        const doc::Body* body = randomBody(200);
        if (!body)
            return;
        std::vector<int> candidates;
        const geom::Shape& s = body->shape();
        for (int i = 0; i < s.faceCount(); ++i)
            if (const auto info = geom::faceInfo(s, i); info && info->kind == geom::SurfaceKind::Cylinder)
                candidates.push_back(i);
        if (candidates.empty())
            return;
        const int f = pickFrom(candidates);
        auto feature = std::make_unique<doc::DeleteFacesFeature>();
        feature->faces = {{f, *geom::captureFaceSignature(s, f)}};
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    // Resize a hole or shaft: move a cylindrical face (OffsetFace).
    void offsetRoundFace()
    {
        const doc::Body* body = randomBody(200);
        if (!body)
            return;
        std::vector<int> candidates;
        const geom::Shape& s = body->shape();
        for (int i = 0; i < s.faceCount(); ++i)
            if (const auto info = geom::faceInfo(s, i); info && info->kind == geom::SurfaceKind::Cylinder)
                candidates.push_back(i);
        if (candidates.empty())
            return;
        const int f = pickFrom(candidates);
        auto feature = std::make_unique<doc::OffsetFaceFeature>();
        feature->face = {f, *geom::captureFaceSignature(s, f)};
        feature->distance = (chance(0.5) ? -1 : 1) * uniform(0.2, 1.0);
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    // A heat-set insert pilot hole at a circular rim.
    void hole()
    {
        const doc::Body* body = randomBody(200);
        if (!body)
            return;
        std::vector<int> rims;
        const geom::Shape& s = body->shape();
        for (int e = 0; e < s.edgeCount(); ++e)
            if (doc::holePlacement(s, e))
                rims.push_back(e);
        if (rims.empty())
            return;
        const int e = pickFrom(rims);
        auto feature = std::make_unique<doc::HoleFeature>();
        feature->rim = {e, *geom::captureEdgeSignature(s, e)};
        feature->diameter = uniform(1.5, 4.0);
        feature->depth = uniform(2, 6);
        (void)push(std::make_unique<cmd::AddFeatureCommand>(body->id(), std::move(feature)));
    }

    void toggleVisible()
    {
        const doc::Body* body = randomBody();
        if (body)
            (void)controller_.setBodyVisible(body->id(), !body->isVisible());
    }

    doc::Document& document_;
    cmd::UndoStack& stack_;
    interact::InteractionController& controller_;
    std::mt19937 rng_;
};

// A document, undo stack and controller, as the application has them.
struct StressSession {
    doc::Document document;
    cmd::UndoStack stack{1000};
    interact::InteractionController controller{document, stack};
    MetricsCache metrics;
    RandomModeler modeler;
    std::vector<std::string> messages;

    explicit StressSession(unsigned seed) : modeler(document, stack, controller, seed)
    {
        controller.setViewportSize({1200, 800});
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
    }

    Snapshot snapshot() { return snapshotOf(document, metrics); }

    // Applies `count` edits (attempting at most 4x as many).
    int build(int count, std::vector<std::string>* log = nullptr)
    {
        int applied = 0;
        for (int attempts = 0; applied < count && attempts < 4 * count; ++attempts) {
            std::string what;
            if (modeler.edit(&what)) {
                ++applied;
                if (log)
                    log->push_back(what);
            }
        }
        return applied;
    }
};

} // namespace os::test
