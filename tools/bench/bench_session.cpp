// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Interaction-level benchmark: builds a small filleted part through the real
// document/command/operation code and times what a user feels. Used for the
// performance table in PROJECT_STATUS.md; rerun before and after any change
// that might affect previews, tessellation or recompute.
//
//   cmake --preset msys2-ucrt64 -DOPENSHAPE_BUILD_TOOLS=ON
//   cmake --build build/msys2-ucrt64 --target bench_session
//   build/msys2-ucrt64/bin/bench_session.exe
//
// OPENSHAPE_LOG=debug also prints the per-operation timers (ScopedTimer).
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "document/Document.h"
#include "document/Feature.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "geometry/TopoSignature.h"
#include "document/SketchProfiles.h"
#include "interaction/InteractionController.h"
#include "interaction/Operation.h"
#include "io/ProjectFile.h"
#include "io/Recovery.h"
#include "selection/PickAccelerator.h"
#include "sketch/Sketch.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>

using namespace os;

namespace {

double timeMs(const std::function<void()>& f)
{
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::vector<doc::EdgeRef> edgesWhere(const geom::Shape& shape, const std::function<bool(const geom::EdgeInfo&)>& pick)
{
    std::vector<doc::EdgeRef> refs;
    for (int i = 0; i < shape.edgeCount(); ++i)
        if (const auto info = geom::edgeInfo(shape, i); info && pick(*info))
            refs.push_back({i, *geom::captureEdgeSignature(shape, i)});
    return refs;
}

int faceWhere(const geom::Shape& shape, const std::function<bool(const geom::FaceInfo&)>& pick)
{
    for (int i = 0; i < shape.faceCount(); ++i)
        if (const auto info = geom::faceInfo(shape, i); info && pick(*info))
            return i;
    return -1;
}

struct Stats {
    double average = 0, worst = 0;
};

Stats timeEach(int count, const std::function<void(int)>& f)
{
    Stats s;
    for (int i = 0; i < count; ++i) {
        const double t = timeMs([&] { f(i); });
        s.average += t / count;
        s.worst = std::max(s.worst, t);
    }
    return s;
}

// A realistic maker part: a 120 x 80 x 40 mm enclosure with rounded corners,
// shelled open at the top (2 mm walls), a floor with a 10 x 6 grid of vent
// holes (one sketch, one through-all cut), four screw bosses with pilot
// holes, and a chamfered rim. Built through the same commands the UI pushes.
int benchEnclosure()
{
    std::printf("\n== Enclosure ==\n");
    doc::Document document;
    cmd::UndoStack stack;
    auto push = [&](std::unique_ptr<cmd::Command> c, const char* what) {
        const Status s = stack.push(std::move(c), document);
        if (!s)
            std::printf("setup step '%s' failed: %s\n", what, s.developerMessage().c_str());
        return s.ok();
    };
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = {-60, -40, 0};
    box->size = {120, 80, 40};
    const Uuid boxId = box->id();
    auto create = std::make_unique<cmd::CreateBodyCommand>("Enclosure", std::move(box));
    const Uuid bodyId = create->bodyId();
    if (!push(std::move(create), "box"))
        return 1;
    auto shape = [&] { return document.body(bodyId)->shape(); };
    auto fillet = [&](double r, std::vector<doc::EdgeRef> edges, const char* what) {
        auto f = std::make_unique<doc::FilletFeature>();
        f->size = r;
        f->edges = std::move(edges);
        return push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(f)), what);
    };
    bool ok = fillet(8.0, edgesWhere(shape(), [](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(std::abs(e.tangent.z) - 1) < 1e-9;
    }), "vertical fillets");
    ok = ok && fillet(3.0, edgesWhere(shape(), [](const geom::EdgeInfo& e) { return std::abs(e.start.z) < 1e-6 && std::abs(e.end.z) < 1e-6; }),
                      "bottom fillets");
    const int top = faceWhere(shape(), [](const geom::FaceInfo& f) { return f.isPlanar() && f.normal.z > 0.999; });
    auto shellStep = std::make_unique<doc::ShellFeature>();
    shellStep->faces = {{top, *geom::captureFaceSignature(shape(), top)}};
    shellStep->thickness = 2.0;
    ok = ok && push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(shellStep)), "shell");

    // Sketches of circles (one per feature, as a user draws them) and the
    // extrusion of all their regions.
    auto circles = [&](const sketch::Plane& plane, const std::vector<Vec2>& centers, double radius, const char* name) {
        sketch::Sketch s(Uuid::generate(), plane);
        s.setName(name);
        s.setHostBody(bodyId);
        for (Vec2 c : centers)
            s.addCircle(s.addPoint(c), radius);
        const Uuid id = s.id();
        ok = ok && push(std::make_unique<cmd::CreateSketchCommand>(s), name);
        return id;
    };
    auto extrudeAll = [&](const Uuid& sketchId, double distance, doc::ExtrudeMode mode, bool throughAll, bool symmetric,
                          const char* what) {
        auto f = std::make_unique<doc::ExtrudeFeature>();
        f->sketchId = sketchId;
        if (auto regions = doc::sketchRegions(*document.sketch(sketchId)))
            for (const auto& r : regions.value())
                f->profiles.push_back(doc::makeProfileRef(r, *document.sketch(sketchId)));
        f->distance = distance;
        f->mode = mode;
        f->throughAll = throughAll;
        f->symmetric = symmetric;
        ok = ok && push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(f)), what);
    };
    auto onPlaneZ = [](double z) { return sketch::Plane{{0, 0, z}, {1, 0, 0}, {0, 1, 0}}; };

    // Floor vents: a 12 x 7 grid, cut through all; their outer rims chamfered.
    std::vector<Vec2> vents;
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 7; ++j)
            vents.push_back({-41.25 + 7.5 * i, -22.5 + 7.5 * j});
    extrudeAll(circles(onPlaneZ(0), vents, 2.0, "Floor vents"), 5, doc::ExtrudeMode::Cut, true, false, "floor vents");
    ok = ok && [&] {
        auto f = std::make_unique<doc::ChamferFeature>();
        f->size = 0.4;
        f->edges = edgesWhere(shape(), [](const geom::EdgeInfo& e) {
            return e.kind == geom::CurveKind::Circle && std::abs(e.center.z) < 1e-6 && std::abs(e.radius - 2) < 1e-6;
        });
        return push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(f)), "vent chamfers");
    }();
    // Side vents: one row through both long walls (a symmetric through-all cut).
    std::vector<Vec2> side;
    for (int i = 0; i < 11; ++i)
        side.push_back({-40.0 + 8.0 * i, 25});
    extrudeAll(circles(sketch::Plane{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}, side, 2.0, "Side vents"), 5, doc::ExtrudeMode::Cut, true,
               true, "side vents");
    // Screw bosses standing on the floor, then their pilot holes.
    const std::vector<Vec2> bosses{{-50, -30}, {50, -30}, {50, 30}, {-50, 30}};
    extrudeAll(circles(onPlaneZ(2.0), bosses, 4.0, "Bosses"), 12.0, doc::ExtrudeMode::Join, false, false, "bosses");
    extrudeAll(circles(onPlaneZ(14.0), bosses, 1.25, "Pilot holes"), -10.0, doc::ExtrudeMode::Cut, false, false, "pilot holes");
    // Chamfer the rim's outer edge (the curves along the top outline).
    ok = ok && [&] {
        auto f = std::make_unique<doc::ChamferFeature>();
        f->size = 0.6;
        f->edges = edgesWhere(shape(), [](const geom::EdgeInfo& e) {
            // Signed distance to the outer outline (a 120 x 80 rounded
            // rectangle, R8): 0 on the outer loop, -2 on the inner one.
            const double qx = std::abs(e.midpoint.x) - 52, qy = std::abs(e.midpoint.y) - 32;
            const double d = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - 8;
            return std::abs(e.start.z - 40) < 1e-6 && std::abs(e.end.z - 40) < 1e-6 && d > -1;
        });
        return push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(f)), "rim chamfer");
    }();
    if (!ok || document.body(bodyId)->hasFailures()) {
        std::printf("enclosure setup failed\n");
        return 1;
    }
    std::size_t triangles = 0;
    const double tessellation = timeMs([&] { triangles = geom::tessellate(shape()).triangleCount(); });
    std::printf("enclosure: %d faces, %d edges, %zu history steps, volume %.0f mm^3\n", shape().faceCount(),
                shape().edgeCount(), document.body(bodyId)->features().size(), geom::volume(shape()));
    std::printf("tessellate body: %.1f ms (%zu triangles)\n", tessellation, triangles);
    {
        const geom::Mesh mesh = geom::tessellate(shape());
        std::size_t nodes = 0;
        const double build = timeMs([&] { nodes = sel::PickAccelerator(mesh).triangleNodeCount(); });
        std::printf("pick accelerator build (once per mesh): %.1f ms (%zu triangle nodes)\n", build, nodes);
    }

    // 1. Push/pull drag on the rim (keeps the chamfer): 8 preview updates.
    const int rim = faceWhere(shape(), [](const geom::FaceInfo& f) { return f.isPlanar() && f.normal.z > 0.999 && f.centroid.z > 39.9; });
    if (auto op = interact::PushPullOperation::create(document, bodyId, rim)) {
        const Stats s = timeEach(8, [&](int i) { op->setValue(op->neutralValue() + 1 + i, document); });
        std::printf("push/pull drag preview on the rim: avg %.1f ms, max %.1f ms (%s)\n", s.average, s.worst,
                    op->error().empty() ? "ok" : op->error().c_str());
        // The same preview split into its kernel and meshing parts.
        doc::PushPullFeature feature;
        feature.face = {rim, *geom::captureFaceSignature(shape(), rim)};
        feature.keepEdges = true;
        double kernel = 0, meshing = 0;
        for (int i = 1; i <= 4; ++i) {
            feature.distance = i;
            Result<geom::Shape> preview = Result<geom::Shape>::failure(ErrorCode::None, "");
            kernel += timeMs([&] { preview = document.preview(bodyId, feature); }) / 4;
            if (preview)
                meshing += timeMs([&] { (void)geom::tessellate(preview.value()); }) / 4;
        }
        std::printf("  of which kernel (split + fuse + checks) %.1f ms, tessellation %.1f ms\n", kernel, meshing);
    }
    // 2. Fillet drag on a boss's top edge: 8 preview updates, 0.2-1.6 mm.
    const int bossEdge = [&] {
        for (int i = 0; i < shape().edgeCount(); ++i)
            if (const auto e = geom::edgeInfo(shape(), i); e && e->kind == geom::CurveKind::Circle && std::abs(e->center.z - 14) < 1e-6
                                                             && std::abs(e->radius - 4) < 1e-6)
                return i;
        return -1;
    }();
    if (auto op = interact::EdgeOperation::create(document, bodyId, {bossEdge}, doc::FeatureKind::Fillet)) {
        const Stats s = timeEach(8, [&](int i) { op->setValue(0.2 * (i + 1), document); });
        std::printf("fillet drag preview on a boss edge: avg %.1f ms, max %.1f ms (%s)\n", s.average, s.worst,
                    op->error().empty() ? "ok" : op->error().c_str());
    }
    // 3. Recompute everything after an upstream edit (box height 40 -> 45).
    const double recompute = timeMs([&] {
        (void)stack.push(std::make_unique<cmd::SetParameterCommand>(boxId, "height", 45.0, false), document);
    });
    std::printf("recompute after box height edit: %.0f ms (failures: %d)\n", recompute, int(document.body(bodyId)->hasFailures()));
    (void)stack.undo(document);

    // 4. Hover picking: the pointer swept over the part in the default view,
    //    as InteractionController does on every mouse move.
    interact::InteractionController controller(document, stack);
    controller.setViewportSize({1400, 900});
    controller.fitAll(false);
    // A 40 x 30 grid over the part's outline on screen.
    const auto box3 = geom::approximateBoundingBox(shape());
    Vec2 lo{1e9, 1e9}, hi{-1e9, -1e9};
    for (int c = 0; c < 8; ++c) {
        const Vec2 p = controller.camera().project({c & 1 ? box3.max.x : box3.min.x, c & 2 ? box3.max.y : box3.min.y,
                                                    c & 4 ? box3.max.z : box3.min.z});
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    std::vector<Vec2> points;
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x)
            points.push_back({lo.x + (hi.x - lo.x) * (x + 0.5) / 40, lo.y + (hi.y - lo.y) * (y + 0.5) / 30});
    int hits = 0;
    const Stats bodies = timeEach(int(points.size()), [&](int i) {
        hits += controller.pickAt(points[std::size_t(i)], interact::InputProfile{}).hit() ? 1 : 0;
    });
    std::printf("hover pick (bodies + sketch profiles, %zu pointer positions, %d hits): avg %.3f ms, max %.3f ms\n",
                points.size(), hits, bodies.average, bodies.worst);
    // Bodies only (the profile regions of the three sketches are the other part).
    for (const auto& sk : document.sketches())
        sk->setVisible(false);
    controller.documentChanged();
    const Stats meshOnly = timeEach(int(points.size()), [&](int i) {
        (void)controller.pickAt(points[std::size_t(i)], interact::InputProfile{});
    });
    std::printf("hover pick (body mesh only): avg %.3f ms, max %.3f ms\n", meshOnly.average, meshOnly.worst);
    return 0;
}

} // namespace

int main()
{
    const char* level = std::getenv("OPENSHAPE_LOG");
    setMinimumLogLevel(level && std::string(level) == "debug" ? LogLevel::Debug : LogLevel::Warning);

    // 20 mm cube, vertical edges R3, top edges R2, +X face pushed out 5 mm.
    doc::Document document;
    cmd::UndoStack stack;
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {20, 20, 20};
    const Uuid boxId = box->id();
    if (!stack.push(std::make_unique<cmd::CreateBodyCommand>("Body 1", std::move(box)), document))
        return 1;
    const Uuid bodyId = document.bodies().front()->id();
    auto shape = [&] { return document.body(bodyId)->shape(); };
    auto addFillet = [&](double r, std::vector<doc::EdgeRef> edges) {
        auto f = std::make_unique<doc::FilletFeature>();
        f->size = r;
        f->edges = std::move(edges);
        return stack.push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(f)), document).ok();
    };
    const bool verticalOk = addFillet(3.0, edgesWhere(shape(), [](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(std::abs(e.tangent.z) - 1) < 1e-9;
    }));
    const bool topOk = addFillet(2.0, edgesWhere(shape(), [](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(e.start.z - 20) < 1e-6 && std::abs(e.end.z - 20) < 1e-6;
    }));
    const int side = faceWhere(shape(), [](const geom::FaceInfo& f) { return f.isPlanar() && f.normal.x > 0.999; });
    auto push = std::make_unique<doc::PushPullFeature>();
    push->face = {side, *geom::captureFaceSignature(shape(), side)};
    push->distance = 5;
    const bool pushOk = stack.push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(push)), document).ok();
    if (!verticalOk || !topOk || !pushOk) {
        std::printf("setup failed (fillets %d/%d, push/pull %d)\n", verticalOk, topOk, pushOk);
        return 1;
    }
    std::printf("part: %d faces, %d edges\n", shape().faceCount(), shape().edgeCount());

    // 1. A drag: eight preview updates of a push/pull on the top face, 1-8 mm
    //    out (the operation's value is the part's height there).
    const int top = faceWhere(shape(), [](const geom::FaceInfo& f) { return f.isPlanar() && f.normal.z > 0.999; });
    auto op = interact::PushPullOperation::create(document, bodyId, top);
    double total = 0, worst = 0;
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
        const double t = timeMs([&] { op->setValue(op->neutralValue() + i, document); });
        total += t;
        worst = std::max(worst, t);
    }
    std::printf("drag preview (push/pull + tessellate): avg %.1f ms, max %.1f ms, %zu triangles\n", total / steps, worst,
                op->previewMesh() ? op->previewMesh()->triangleCount() : std::size_t(0));

    // 2. Display mesh of the committed body.
    std::size_t triangles = 0;
    const double tessellation = timeMs([&] { triangles = geom::tessellate(shape()).triangleCount(); });
    std::printf("tessellate body: %.2f ms (%zu triangles)\n", tessellation, triangles);

    // 3. Recompute after an upstream edit (box height), incl. reference resolution.
    const double recompute = timeMs([&] {
        (void)stack.push(std::make_unique<cmd::SetParameterCommand>(boxId, "height", 25.0, false), document);
    });
    std::printf("recompute after box height edit: %.1f ms (failures: %d)\n", recompute,
                int(document.body(bodyId)->hasFailures()));

    // 4. Tight bounding box: first call and cached repeats.
    const auto s = shape();
    const double first = timeMs([&] { (void)geom::boundingBox(s); });
    const double repeats = timeMs([&] {
        for (int i = 0; i < 10; ++i)
            (void)geom::boundingBox(s);
    });
    std::printf("boundingBox: first %.2f ms, next 10 calls %.3f ms\n", first, repeats);

    // 5. Recovery copy (written on the GUI thread a few seconds after edits
    //    settle) and a full Save, on this part and on a heavy one: the part
    //    patterned 8 x 6 (48 filleted blocks), plus 20 more filleted bodies.
    const auto dir = std::filesystem::temp_directory_path() / "openshape_bench_recovery";
    std::filesystem::create_directories(dir);
    const io::RecoveryStore store(dir);
    const std::string session = Uuid::generate().toString();
    auto timeSaves = [&](const char* what) {
        io::SaveOptions noCache;
        noCache.includeGeometryCache = false;
        double serialize = 0, archive = 0, write = 0;
        std::size_t bytes = 0;
        const int n = 5;
        for (int i = 0; i < n; ++i) {
            io::ProjectData data;
            serialize += timeMs([&] { data = io::serializeProject(document, noCache); });
            Result<std::string> zip = Result<std::string>::failure(ErrorCode::None, "");
            archive += timeMs([&] { zip = io::buildProjectArchive(data); });
            bytes = zip ? zip.value().size() : 0;
            write += timeMs([&] { (void)io::writeFileAtomically(dir / "copy.openshape", zip ? zip.value() : std::string()); });
        }
        const double storeWrite = timeMs([&] { (void)store.write(session, document, {}); });
        const double full = timeMs([&] { (void)io::saveProject(document, dir / "full.openshape"); });
        std::printf("%s: recovery copy %.2f ms (serialize %.2f + zip %.2f + write %.2f; %zu bytes), store.write %.2f ms; "
                    "full save with geometry cache %.1f ms (%ju bytes)\n",
                    what, (serialize + archive + write) / n, serialize / n, archive / n, write / n, bytes, storeWrite, full,
                    std::uintmax_t(std::filesystem::file_size(dir / "full.openshape")));
    };
    timeSaves("filleted part");

    auto pattern = [&](doc::PatternFeature::Layout layout, int count) {
        auto p = std::make_unique<doc::PatternFeature>();
        p->layout = layout;
        p->count = count;
        p->spacing = 30;
        p->axisOrigin = {0, -60, 0};
        return stack.push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(p)), document).ok();
    };
    const bool linearOk = pattern(doc::PatternFeature::Layout::Linear, 8);
    const bool circularOk = pattern(doc::PatternFeature::Layout::Circular, 6);
    for (int i = 0; i < 20; ++i) {
        auto cube = std::make_unique<doc::BoxFeature>();
        cube->size = {10, 10, 10};
        (void)stack.push(std::make_unique<cmd::CreateBodyCommand>("Body " + std::to_string(i + 2), std::move(cube)), document);
        const Uuid extra = document.bodies().back()->id();
        auto f = std::make_unique<doc::FilletFeature>();
        f->size = 1;
        f->edges = edgesWhere(document.body(extra)->shape(), [](const geom::EdgeInfo&) { return true; });
        (void)stack.push(std::make_unique<cmd::AddFeatureCommand>(extra, std::move(f)), document);
    }
    int faces = 0;
    for (const auto& b : document.bodies())
        faces += b->shape().faceCount();
    std::printf("heavy model: %zu bodies, %d faces, %zu steps in body 1 (patterns ok: %d %d)\n", document.bodies().size(),
                faces, document.body(bodyId)->features().size(), int(linearOk), int(circularOk));
    timeSaves("heavy model");
    std::filesystem::remove_all(dir);
    return benchEnclosure();
}
