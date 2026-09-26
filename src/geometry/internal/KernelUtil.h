// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// Private to the geometry library: helpers shared by the kernel-facing
// implementation files. Do not include from other layers.

#include "core/Log.h"
#include "core/Result.h"
#include "geometry/Shape.h"

#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <sstream>
#include <string>

namespace os::geom::detail {

inline gp_Pnt toPnt(const Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
inline gp_Vec toVec(const Vec3& v) { return gp_Vec(v.x, v.y, v.z); }
inline Vec3 fromPnt(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
inline Vec3 fromDir(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }
inline Vec3 fromVec(const gp_Vec& d) { return {d.X(), d.Y(), d.Z()}; }

// Minimum length accepted for any dimension (kernel confusion tolerance is 1e-7).
inline constexpr double kMinLength = 1e-6;

inline std::string describeFailure(const Standard_Failure& failure)
{
    std::ostringstream out;
    out << failure.DynamicType()->Name();
    if (failure.GetMessageString() && *failure.GetMessageString())
        out << ": " << failure.GetMessageString();
    return out.str();
}

template <typename Algo>
std::string describeAlgoErrors(const Algo& algo)
{
    std::ostringstream out;
    algo.DumpErrors(out);
    return out.str();
}

// Lets OpenCASCADE set up its signal handlers once per process
// (idempotent) and keeps them aside: they are only active inside kernel
// calls (KernelSignalScope). Many OCCT algorithms (booleans, fillets) also
// catch faults internally and report a failed build instead.
void installKernelSignalHandlers();

// While one exists (on any thread), OpenCASCADE's signal handlers are
// installed, so a fault inside a kernel call becomes a failed step. When the
// last one ends, the handlers that were there before come back: outside
// kernel calls a fault is a real crash for the app's crash handler and the
// operating system (OCCT's handlers would end the app with exit(1)). On
// Windows the C runtime calls C signal handlers from an SEH handler around
// main and every thread it starts, before any top-level exception filter,
// and keeps them per thread; ours (onKernelSignal in Modeling.cpp) jumps
// back as OCCT's does, without OCCT's mutex, which a jump leaves locked (a
// fault on a second thread would then wait forever). On POSIX handlers
// belong to the process, so a dispatcher is installed instead: it hands a
// fault to OCCT only on the thread inside the kernel call (the kernel lock
// owner) and to the handler from before on any other thread (e.g. a crash
// on the GUI thread while the preview worker is in the kernel).
//
// It also holds the kernel lock (see KernelLock below) for its lifetime.
class KernelSignalScope {
public:
    KernelSignalScope();
    ~KernelSignalScope();
    KernelSignalScope(const KernelSignalScope&) = delete;
    KernelSignalScope& operator=(const KernelSignalScope&) = delete;

private:
    int entryDepth_; // the kernel lock depth before this scope; restored at its end
};

// The kernel lock: one thread at a time runs OpenCASCADE code (TD-4). Meshing
// writes triangulations into faces and edges that other shapes share (a
// preview result shares most faces with the body it came from), and nearly
// every kernel algorithm reads those edges' lists of representations, so a
// lock around meshing alone would not do. Recursive. Every kernel call takes
// it: the OS_KERNEL_SIGNALS_TO_EXCEPTIONS scope (and so guarded()) does, and
// functions that touch kernel data without one hold a KernelLock. When a
// kernel fault jumps back to a scope, that scope's end also releases the
// KernelLocks the jump skipped.
int kernelLockDepth(); // the calling thread's depth (0 = it does not hold the lock)
void lockKernel();
void unlockKernelTo(int depth);

class KernelLock {
public:
    KernelLock() : entryDepth_(kernelLockDepth()) { lockKernel(); }
    ~KernelLock() { unlockKernelTo(entryDepth_); }
    KernelLock(const KernelLock&) = delete;
    KernelLock& operator=(const KernelLock&) = delete;

private:
    int entryDepth_;
};

// The first statement of every try block around kernel calls (guarded()
// uses it). With OCC_CONVERT_SIGNALS (how MSYS2, Homebrew and Linux builds
// of OCCT are compiled) an access violation inside the kernel jumps back to
// this point and is rethrown as a Standard_Failure. Keep locks and other
// state that must be released outside the try block. The scope is declared
// before the jump target, so it is still alive when a fault jumps back and
// ends normally when the exception leaves the try block.
#define OS_KERNEL_SIGNALS_TO_EXCEPTIONS                                                                                \
    const ::os::geom::detail::KernelSignalScope osKernelSignalScope;                                                    \
    OCC_CATCH_SIGNALS

// Runs `fn` and converts any kernel exception into a failed Result of the
// same type `fn` returns.
template <typename Fn>
auto guarded(const char* operation, const char* userMessage, Fn&& fn) -> decltype(fn())
{
    using R = decltype(fn());
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        return fn();
    } catch (const Standard_Failure& failure) {
        const std::string dev = std::string(operation) + " threw " + describeFailure(failure);
        OS_LOG(Error, Kernel) << dev;
        return R::failure(ErrorCode::KernelFailure, userMessage, dev);
    } catch (const std::exception& e) {
        const std::string dev = std::string(operation) + " threw std::exception: " + e.what();
        OS_LOG(Error, Kernel) << dev;
        return R::failure(ErrorCode::KernelFailure, userMessage, dev);
    }
}

// Normalizes a kernel result: unwraps single-solid compounds, rejects empty
// results, and runs the B-rep validity checker.
Result<Shape> finishSolid(const TopoDS_Shape& result, const char* operation, const char* userMessage);

inline gp_Dir toDir(const Vec3& v) { return gp_Dir(v.x, v.y, v.z); }

} // namespace os::geom::detail
