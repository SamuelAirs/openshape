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
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#endif

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

namespace {
std::atomic<int> g_outsideFaults{0};
extern "C" void countOutsideFault(int)
{
    ++g_outsideFaults;
}
} // namespace

// A fault on another thread while the worker is in a kernel call is not the
// kernel's: it reaches the handler that was there before (the app's crash
// log), not OpenCASCADE's, which would end the app with exit(1) - no crash
// log, no crash report. On POSIX signal handlers belong to the process
// (Modeling.cpp dispatches by thread); the Windows C runtime keeps them per
// thread.
TEST(KernelThreads, FaultOnAnotherThreadDuringAKernelCallIsNotTheKernels)
{
    geom::installKernelSignalHandling();
    // The app's crash handler stands in: one that counts (a raised signal
    // returns from its handler).
#if defined(_WIN32)
    const auto previous = std::signal(SIGSEGV, countOutsideFault);
#else
    struct sigaction count;
    struct sigaction previous;
    std::memset(&count, 0, sizeof count);
    count.sa_handler = countOutsideFault;
    sigemptyset(&count.sa_mask);
    ASSERT_EQ(sigaction(SIGSEGV, &count, &previous), 0);
#endif
    g_outsideFaults = 0;
    std::atomic<bool> inside{false}, release{false};
    std::thread worker([&] {
        geom::runInsideKernelCallForTesting([&] {
            inside = true;
            while (!release)
                std::this_thread::yield();
        });
    });
    while (!inside)
        std::this_thread::yield();
    std::raise(SIGSEGV); // on this thread, in no kernel call
    release = true;
    worker.join();
    EXPECT_EQ(g_outsideFaults.load(), 1) << "a fault outside the kernel did not reach the handler from before";
#if !defined(_WIN32)
    // Back in place after the kernel call.
    struct sigaction now;
    ASSERT_EQ(sigaction(SIGSEGV, nullptr, &now), 0);
    EXPECT_TRUE((now.sa_flags & SA_SIGINFO) == 0 && now.sa_handler == countOutsideFault);
    sigaction(SIGSEGV, &previous, nullptr);
#else
    std::signal(SIGSEGV, previous);
#endif
    // Faults inside kernel calls are still contained.
    EXPECT_TRUE(geom::simulateKernelFault());
}

// Kernel faults on two threads in turn (the preview worker's, then the GUI
// thread's on the same step when it is applied, and again): each comes back
// as a failure. OpenCASCADE's own Windows handler leaves a process-wide
// mutex locked after a fault, so the next fault on another thread would
// wait for it forever (Modeling.cpp, onKernelSignal).
TEST(KernelThreads, FaultsOnTwoThreadsInTurnAreAllContained)
{
    std::promise<int> result;
    std::future<int> done = result.get_future();
    std::thread driver([&result] {
        int contained = 0;
        for (int round = 0; round < 3; ++round) {
            contained += geom::simulateKernelFault() ? 1 : 0;
            bool other = false;
            std::thread worker([&other] { other = geom::simulateKernelFault(); });
            worker.join();
            contained += other ? 1 : 0;
        }
        result.set_value(contained);
    });
    if (done.wait_for(std::chrono::seconds(60)) != std::future_status::ready) {
        std::fprintf(stderr, "a kernel fault on a second thread waits forever for the first one's fault handling\n");
        std::fflush(stderr);
        std::_Exit(1); // the stuck thread cannot be joined
    }
    driver.join();
    EXPECT_EQ(done.get(), 6) << "every fault must come back as a failure";
    // The kernel works on both threads afterwards.
    EXPECT_TRUE(geom::makeBox({0, 0, 0}, {1, 1, 1}).ok());
    bool ok = false;
    std::thread after([&ok] { ok = geom::makeBox({0, 0, 0}, {2, 2, 2}).ok(); });
    after.join();
    EXPECT_TRUE(ok);
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

// Previews are meshed on a copy of their topology: a preview result shares
// most faces and edges with the body, and meshing it in place left polygons
// on the body's edges with every preview (a filleted cube's BRep text grew
// from 93 KB to 1.3 MB over 40 previews). The copy's mesh is complete and
// has the original's face ids.
TEST(KernelThreads, IsolatedMeshingLeavesSharedEdgesAlone)
{
    const SharedParts parts = sharedParts();
    (void)geom::tessellate(parts.rounded); // the body's display mesh, in place
    int top = -1;
    for (int i = 0; i < parts.rounded.faceCount(); ++i)
        if (const auto info = geom::faceInfo(parts.rounded, i); info && info->isPlanar() && info->normal.z > 0.999)
            top = i;
    ASSERT_GE(top, 0);
    const std::size_t before = geom::toBrepString(parts.rounded).size();
    geom::TessellationParams isolated;
    isolated.isolated = true;
    for (int i = 1; i <= 12; ++i) {
        const auto preview = geom::pushPullFace(parts.rounded, top, 0.5 * i);
        ASSERT_TRUE(preview.ok());
        const geom::Mesh mesh = geom::tessellate(preview.value(), isolated);
        ASSERT_GT(mesh.triangleCount(), 0u);
        EXPECT_NEAR(meshHeight(mesh), 40.0 + 0.5 * i, 1e-4);
        ASSERT_EQ(mesh.faceCount(), preview.value().faceCount());
        // Each face's triangles cover that face (the ids are the shape's).
        for (int f = 0; f < mesh.faceCount(); ++f) {
            double area = 0;
            for (std::uint32_t t = mesh.faceTriangleOffset[std::size_t(f)]; t < mesh.faceTriangleOffset[std::size_t(f) + 1];
                 ++t) {
                const Vec3 a = mesh.vertex(mesh.indices[3 * t]);
                const Vec3 b = mesh.vertex(mesh.indices[3 * t + 1]);
                const Vec3 c = mesh.vertex(mesh.indices[3 * t + 2]);
                area += (b - a).cross(c - a).length() / 2;
            }
            const double exact = geom::faceInfo(preview.value(), f)->area;
            EXPECT_NEAR(area, exact, 0.02 * exact + 0.05) << "face " << f;
        }
    }
    EXPECT_EQ(geom::toBrepString(parts.rounded).size(), before) << "previews left meshes on the body's edges";
    // Meshing in place does leave them (why previews are isolated).
    const auto preview = geom::pushPullFace(parts.rounded, top, 7.0);
    ASSERT_TRUE(preview.ok());
    (void)geom::tessellate(preview.value());
    EXPECT_GT(geom::toBrepString(parts.rounded).size(), before);
}
