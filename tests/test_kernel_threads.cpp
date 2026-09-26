// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The kernel from several threads (TD-4): previews are computed and meshed on
// a worker thread while the GUI thread keeps the document's shapes, which
// share faces and edges with every preview. OpenCASCADE code runs under one
// process-wide lock, a kernel fault on the worker becomes a failure there as
// on the GUI thread, and the GUI thread's waits for the lock are counted.
#include "geometry/KernelSignals.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace os;

namespace {

// A 40 mm box with all edges rounded (R4), and the same part with its top
// pushed up 5 mm: the second shares most faces and edges with the first.
struct SharedParts {
    geom::Shape rounded;
    geom::Shape pushed;
};

SharedParts sharedParts()
{
    SharedParts parts;
    const auto box = geom::makeBox({0, 0, 0}, {40, 40, 40});
    EXPECT_TRUE(box.ok());
    std::vector<int> edges;
    for (int i = 0; i < box.value().edgeCount(); ++i)
        edges.push_back(i);
    const auto rounded = geom::filletEdges(box.value(), edges, 4.0, {});
    EXPECT_TRUE(rounded.ok()) << rounded.developerMessage();
    parts.rounded = rounded.value();
    int top = -1;
    for (int i = 0; i < parts.rounded.faceCount(); ++i)
        if (const auto info = geom::faceInfo(parts.rounded, i); info && info->isPlanar() && info->normal.z > 0.999)
            top = i;
    const auto pushed = geom::pushPullFace(parts.rounded, top, 5.0);
    EXPECT_TRUE(pushed.ok()) << pushed.developerMessage();
    parts.pushed = pushed.value();
    return parts;
}

double meshHeight(const geom::Mesh& mesh)
{
    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 2; i < mesh.positions.size(); i += 3) {
        lo = std::min(lo, double(mesh.positions[i]));
        hi = std::max(hi, double(mesh.positions[i]));
    }
    return hi - lo;
}

} // namespace

TEST(KernelThreads, FaultOnAWorkerThreadBecomesAFailure)
{
    bool contained = false;
    std::thread worker([&contained] { contained = geom::simulateKernelFault(); });
    worker.join();
    EXPECT_TRUE(contained) << "an access violation in a kernel call on a worker thread must come back as a failure";
    // The kernel lock was released by the scope the fault jumped back to.
    std::atomic<bool> done{false};
    std::thread other([&done] {
        EXPECT_TRUE(geom::makeBox({0, 0, 0}, {1, 1, 1}).ok());
        done = true;
    });
    other.join();
    EXPECT_TRUE(done.load());
    EXPECT_TRUE(geom::makeBox({0, 0, 0}, {2, 2, 2}).ok());
}

TEST(KernelThreads, SharedShapesMeshedAndModeledFromTwoThreads)
{
    const SharedParts parts = sharedParts();
    ASSERT_FALSE(parts.rounded.isNull());
    ASSERT_FALSE(parts.pushed.isNull());
    // One thread meshes the pushed part while the other meshes the rounded
    // one and computes fillets and volumes on it, many times over: meshing
    // writes into the faces and edges both shapes share.
    std::atomic<int> failures{0};
    std::thread worker([&] {
        for (int i = 0; i < 12; ++i) {
            const geom::Mesh mesh = geom::tessellate(parts.pushed);
            if (mesh.triangleCount() == 0 || std::abs(meshHeight(mesh) - 45.0) > 0.05)
                ++failures;
        }
    });
    for (int i = 0; i < 12; ++i) {
        const geom::Mesh mesh = geom::tessellate(parts.rounded);
        EXPECT_GT(mesh.triangleCount(), 0u);
        EXPECT_NEAR(meshHeight(mesh), 40.0, 0.05);
        const auto thicker = geom::pushPullFace(parts.rounded, 0, 1.0 + i);
        EXPECT_TRUE(thicker.ok() || !thicker.userMessage().empty());
        EXPECT_GT(geom::volume(parts.rounded), 0.0);
    }
    worker.join();
    EXPECT_EQ(failures.load(), 0);
    EXPECT_NEAR(geom::boundingBox(parts.pushed).size().z, 45.0, 1e-6);
}

TEST(KernelThreads, InteractiveThreadWaitsAreCounted)
{
    const SharedParts parts = sharedParts();
    geom::setInteractiveThread();
    geom::resetInteractiveKernelWaits();
    const std::uint64_t callsBefore = geom::kernelCallsOnThisThread();

    // The worker holds the kernel for a long call (a very fine mesh).
    std::atomic<bool> started{false};
    std::thread worker([&] {
        started = true;
        geom::TessellationParams fine;
        fine.adaptive = false;
        fine.linearDeflection = 0.002;
        fine.angularDeflection = 0.02;
        (void)geom::tessellate(parts.pushed, fine);
    });
    while (!started)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto t0 = std::chrono::steady_clock::now();
    const double volume = geom::volume(parts.rounded);
    const double waitedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    worker.join();

    EXPECT_GT(volume, 0.0);
    EXPECT_GT(geom::kernelCallsOnThisThread(), callsBefore);
    RecordProperty("waitedMs", std::to_string(waitedMs));
    const geom::KernelWaits waits = geom::interactiveKernelWaits();
    // Either the worker was still meshing (then the wait was counted) or it
    // was already done (no wait); a long wait must always be counted.
    if (waitedMs > 20) {
        EXPECT_GE(waits.count, 1u);
        EXPECT_GT(waits.longestMs, 10.0);
    }
    EXPECT_LE(waits.longestMs, waitedMs + 1.0);
    // Another thread's waits are not the interactive thread's.
    geom::resetInteractiveKernelWaits();
    std::thread other([&] { (void)geom::volume(parts.pushed); });
    other.join();
    EXPECT_EQ(geom::interactiveKernelWaits().count, 0u);
}
