// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace os::geom {

// OpenCASCADE turns an access violation inside a guarded kernel call into a
// failed step: its signal handlers jump back to the call. Outside kernel
// calls the same handlers would end the app with exit(1), hiding a real
// crash from the crash log and from the operating system's crash reports.
// So the application's crash handler installs this first, then puts itself
// in front and hands a fault over only while insideKernelCall() is true.
void installKernelSignalHandling();

// Whether the calling thread is inside a guarded kernel call.
bool insideKernelCall();

// For tests (--simulate-kernel-fault): an access violation inside a guarded
// kernel call. True if it came back as a failure, as it should.
bool simulateKernelFault();

// For tests: runs `fn` inside a guarded kernel call on the calling thread
// (it holds the kernel lock, and OpenCASCADE's signal handling is active for
// this thread only: a fault on another thread meanwhile is a real crash).
void runInsideKernelCallForTesting(const std::function<void()>& fn);

// ---- Threads ----------------------------------------------------------------
// OpenCASCADE runs on one thread at a time: every kernel call takes one
// process-wide lock (geometry/internal/KernelUtil.h, KernelLock). Meshing
// writes into faces and edges that other shapes share, so without it the
// preview worker and the GUI thread could not use shapes at the same time
// (TD-4). The GUI thread makes no kernel calls while dragging; when it does
// need the kernel while a preview computes, it waits for the worker's
// current kernel call. These waits are what the user feels, so they are
// counted for the thread marked interactive (and logged from 1 ms on, at
// debug level, as "gui: waiting for the kernel ... took N ms").

// Marks the calling thread as the interactive (GUI) thread.
void setInteractiveThread();

struct KernelWaits {
    std::uint64_t count = 0;
    double totalMs = 0;
    double longestMs = 0;
};
// How often and how long the interactive thread waited for the kernel lock.
KernelWaits interactiveKernelWaits();
void resetInteractiveKernelWaits();

// Kernel calls made by the calling thread so far (tests: a drag step makes
// none on the GUI thread when previews run on the worker).
std::uint64_t kernelCallsOnThisThread();

// ---- Time limits ----------------------------------------------------------
// A kernel algorithm that runs away (a boolean spinning on nearly tangent
// faces, meshing or reading damaged geometry) would freeze the app for
// good. The long ones - booleans, fillets, chamfers, shells, face removal,
// meshing and reading stored geometry - are given a progress range that
// stops them once this thread's limit has passed; the step then fails with
// "This took too long and was stopped." Each algorithm gets the full limit.
inline constexpr std::chrono::milliseconds kDefaultKernelTimeLimit{60'000}; // recompute, file reads (an iPhone is slower)
inline constexpr std::chrono::milliseconds kPreviewKernelTimeLimit{25'000}; // the preview worker

// The calling thread's limit (kDefaultKernelTimeLimit unless set).
std::chrono::milliseconds kernelTimeLimit();

// Sets the calling thread's limit while it exists (nests; restores the
// previous one). The preview worker holds one for its lifetime; tests use a
// zero limit to stop algorithms at their first progress check.
class ScopedKernelTimeLimit {
public:
    explicit ScopedKernelTimeLimit(std::chrono::milliseconds limit);
    ~ScopedKernelTimeLimit();
    ScopedKernelTimeLimit(const ScopedKernelTimeLimit&) = delete;
    ScopedKernelTimeLimit& operator=(const ScopedKernelTimeLimit&) = delete;

private:
    std::chrono::milliseconds previous_;
};

} // namespace os::geom
