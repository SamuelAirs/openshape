// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions of this file are adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE).

#pragma once

// Private to the geometry library: helpers shared by the kernel-facing
// implementation files. Do not include from other layers.

#include "core/Log.h"
#include "core/Result.h"
#include "geometry/Shape.h"

#include <BOPAlgo_Operation.hxx>
#include <Standard_ErrorHandler.hxx>
#include <TopTools_ListOfShape.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <sstream>
#include <string>
#include <vector>

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
// fault to OCCT only when the faulting thread is inside a kernel try block
// (Standard_ErrorHandler::IsInTryBlock, per thread) and to the handler from
// before otherwise (e.g. a crash on the GUI thread while the preview worker
// is in the kernel). On both, a fault outside any try block stays a crash.
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

// The volume of a shape (signed: negative for an inside-out solid), and its
// centre of mass when asked. Adaptive Gauss-Kronrod integration per knot
// span to 1e-7 relative: the fixed-order Gauss rule of
// BRepGProp::VolumeProperties is exact for planes and analytic surfaces but
// not across a B-spline's knot spans. Adapted from OS3DVolume.
double exactVolume(const TopoDS_Shape& shape, gp_Pnt* centroid = nullptr);

// A copy with its own topology (new vertices, edges, faces...) on the same
// curves and surfaces, without meshes. Cheap. For kernel algorithms that
// write into the shapes they are given (sweeps add pcurves to the profile's
// edges, the offset raises tolerances, ShapeFix rewrites edges): the copy
// takes the writes, and cached step outputs, undo states and imported
// bodies stay as they were.
TopoDS_Shape independentCopy(const TopoDS_Shape& shape);

// Whether the shape's geometry is valid (BRepCheck_Analyzer). The checker
// also judges the display mesh a shape carries (edge polygons on face
// triangulations), which is not model data: a shape that fails only because
// of it is valid. Adapted from OS3DIsValid.
bool validGeometry(const TopoDS_Shape& shape);

// The largest tolerance any sub-shape of the shape carries.
double maxTolerance(const TopoDS_Shape& shape);

// What finishSolid may fall back on.
struct FinishOptions {
    // The result before its faces were merged (unifyCopy): used when the
    // merged one is invalid and this one is not.
    TopoDS_Shape unmerged;
    // The operation's inputs: their tolerances raise the cap on a repair.
    std::vector<TopoDS_Shape> inputs;
};

// Normalizes a kernel result: unwraps single-solid compounds, rejects empty
// results and checks validity. An invalid result gets one repair attempt
// (ShapeFix_Shape on an independent copy), kept only if it is valid and its
// largest tolerance stays within max(1% of its diagonal, 10x the largest
// tolerance before, the inputs' included): a repair that only passes the
// checker by loosening tolerances to part size makes later booleans see
// touching faces everywhere and silently do nothing. A kept repair is noted
// in the result's developer message and logged.
Result<Shape> finishSolid(const TopoDS_Shape& result, const char* operation, const char* userMessage,
                          const FinishOptions& options = {});

// One boolean (fuse, cut or common) of `arguments` with `tools`, leaving
// its inputs untouched (non-destructive: by default OCCT may raise
// tolerances of the arguments' sub-shapes in place, and those belong to
// cached step outputs, undo states and imported bodies). Fuzzy: the value
// is the operands' bounding diagonal x Precision::Confusion(), so faces
// that meet within rounding of each other are treated as touching; on an
// error it is retried once at 10x. It runs under a time limit (Deadline.h).
// Adapted from the fuzzy boolean ladder of OpenShape3D.
struct BooleanRun {
    TopoDS_Shape shape;  // null on failure
    std::string errors;  // the kernel's errors (the last attempt's)
    bool timedOut = false;
    double fuzzy = 0;    // the fuzzy value of the attempt that built `shape`
};
BooleanRun runBoolean(BOPAlgo_Operation operation, const TopTools_ListOfShape& arguments, const TopTools_ListOfShape& tools);
BooleanRun runBoolean(BOPAlgo_Operation operation, const TopoDS_Shape& argument, const TopoDS_Shape& tool);

// Merges faces and edges that lie on the same surface or curve
// (ShapeUpgrade_UnifySameDomain) on a copy of `built` that shares its
// geometry and meshes: the merge rewrites edges of the shape it is given in
// place, and a kernel result shares its untouched sub-shapes with the
// operation's inputs. Returns `built` itself when the merge throws.
// Adapted from OS3DUnified and the boolean result path of OpenShape3D.
TopoDS_Shape unifyCopy(const TopoDS_Shape& built);

// unifyCopy, then finishSolid with the unmerged shape as its fallback.
Result<Shape> finishMerged(const TopoDS_Shape& built, const char* operation, const char* userMessage,
                           std::vector<TopoDS_Shape> inputs = {});

// What BRepCheck rejects, for logs: "FACE: BRepCheck_UnorientableShape x3;
// EDGE in FACE: BRepCheck_InvalidCurveOnSurface; ...". Statuses a
// sub-shape has only within one parent (a wire that self-intersects in its
// face, a vertex off an edge's curve) are named "SUB in PARENT".
std::string describeCheckFailures(const BRepCheck_Analyzer& analyzer, const TopoDS_Shape& shape);

inline gp_Dir toDir(const Vec3& v) { return gp_Dir(v.x, v.y, v.z); }

} // namespace os::geom::detail
