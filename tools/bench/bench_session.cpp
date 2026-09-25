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
#include "interaction/Operation.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    return 0;
}
