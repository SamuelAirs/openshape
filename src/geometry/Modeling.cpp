// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Modeling.h"
#include "geometry/KernelSignals.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <ElSLib.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_Surface.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Precision.hxx>
#include <Message_Report.hxx>
#include <OSD.hxx>
#include <OSD_Exception_ACCESS_VIOLATION.hxx>
#include <OSD_Exception_ILLEGAL_INSTRUCTION.hxx>
#include <Standard_NumericError.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <gp_Ax2.hxx>
#include <gp_Lin.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>

#if !defined(_WIN32)
#include <pthread.h>
#include <signal.h>
#endif

namespace os::geom {

namespace detail {

namespace {

#if defined(_WIN32)
constexpr int kKernelSignals[] = {SIGSEGV, SIGILL, SIGFPE};
using SignalHandler = void (*)(int);
SignalHandler g_outsideHandlers[std::size(kKernelSignals)] = {};

// Installed (per thread: the C runtime keeps these handlers per thread) while
// a kernel call runs. It does what OpenCASCADE's own handler does - jump
// back to the kernel call's try block, where the fault becomes a failure -
// but without that handler's process-wide mutex: OCCT locks it and jumps
// with it still locked (the jump skips the release). The faulting thread
// could lock it again, but the next fault on any other thread (the GUI
// thread after the preview worker's, or the other way round) would wait
// for it forever: the app would freeze instead of reporting a failure.
extern "C" void onKernelSignal(int signal)
{
    // The C runtime resets the handler before calling it.
    std::signal(signal, onKernelSignal);
    switch (signal) {
    case SIGSEGV: OSD_Exception_ACCESS_VIOLATION::NewInstance("ACCESS VIOLATION in a kernel call")->Jump(); break;
    case SIGILL: OSD_Exception_ILLEGAL_INSTRUCTION::NewInstance("ILLEGAL INSTRUCTION in a kernel call")->Jump(); break;
    default: Standard_NumericError::NewInstance("ARITHMETIC ERROR in a kernel call")->Jump(); break;
    }
}
#else
constexpr int kKernelSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
struct sigaction g_kernelActions[std::size(kKernelSignals)];  // OpenCASCADE's
struct sigaction g_outsideActions[std::size(kKernelSignals)]; // in place outside kernel calls (the crash log's)
struct sigaction g_dispatchActions[std::size(kKernelSignals)]; // installed while a kernel call runs
// sigaction handlers belong to the whole process: while the preview worker
// is in the kernel, a crash on the GUI thread would reach OpenCASCADE's
// handler, which ends the app with exit(1) when the faulting thread is in no
// kernel call (no crash log, no crash report). So kernel calls install a
// dispatcher that hands the fault to OpenCASCADE only on the thread inside
// the kernel call: one at a time (the kernel lock), recorded here.
static_assert(std::atomic<pthread_t>::is_always_lock_free, "read in a signal handler");
std::atomic<bool> g_kernelThreadSet{false};
std::atomic<pthread_t> g_kernelThread{};

int kernelSignalIndex(int signal)
{
    for (std::size_t i = 0; i < std::size(kKernelSignals); ++i)
        if (kKernelSignals[i] == signal)
            return static_cast<int>(i);
    return -1;
}

void forwardSignal(const struct sigaction* action, int signal, siginfo_t* info, void* context)
{
    if (action && (action->sa_flags & SA_SIGINFO) != 0) {
        if (action->sa_sigaction) {
            action->sa_sigaction(signal, info, context);
            return;
        }
    } else if (action && action->sa_handler != SIG_DFL && action->sa_handler != SIG_IGN) {
        action->sa_handler(signal);
        return;
    }
    // No handler to hand it to: the default action, a crash the system
    // reports (the raised signal is delivered when this handler returns).
    struct sigaction fallback;
    std::memset(&fallback, 0, sizeof fallback);
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(signal, &fallback, nullptr);
    raise(signal);
}

extern "C" void onKernelSignal(int signal, siginfo_t* info, void* context)
{
    const int index = kernelSignalIndex(signal);
    const bool kernelThread = g_kernelThreadSet.load() && pthread_equal(g_kernelThread.load(), pthread_self()) != 0;
    const struct sigaction* action = index < 0 ? nullptr
                                   : kernelThread ? &g_kernelActions[index]
                                                  : &g_outsideActions[index];
    forwardSignal(action, signal, info, context);
}
#endif
std::mutex g_signalMutex;
int g_kernelCalls = 0; // kernel calls running now, on all threads

// The kernel lock: recursive, and its owner's depth can be set back (a
// kernel fault jumps over the release of locks taken after the catch point).
struct KernelMutex {
    std::mutex mutex;
    std::condition_variable released;
    std::thread::id owner;
    int depth = 0;
};

KernelMutex& kernelMutex()
{
    static KernelMutex m;
    return m;
}

thread_local std::uint64_t t_kernelCalls = 0;

// The interactive (GUI) thread and how long it waited for the kernel.
struct InteractiveWaits {
    std::mutex mutex;
    std::thread::id thread;
    KernelWaits waits;
};

InteractiveWaits& interactiveWaits()
{
    static InteractiveWaits w;
    return w;
}

void noteWait(double ms)
{
    auto& w = interactiveWaits();
    {
        const std::lock_guard lock(w.mutex);
        if (w.thread != std::this_thread::get_id())
            return;
        ++w.waits.count;
        w.waits.totalMs += ms;
        w.waits.longestMs = std::max(w.waits.longestMs, ms);
    }
    // "took" so scripts/dev/watch_log.py counts it among the slow steps.
    if (ms >= 1.0)
        OS_LOG(Debug, Performance) << "gui: waiting for the kernel (preview worker) took " << ms << " ms";
}

} // namespace

int kernelLockDepth()
{
    auto& k = kernelMutex();
    const std::lock_guard lock(k.mutex);
    return k.owner == std::this_thread::get_id() ? k.depth : 0;
}

void lockKernel()
{
    auto& k = kernelMutex();
    const auto self = std::this_thread::get_id();
    std::unique_lock lock(k.mutex);
    ++t_kernelCalls;
    if (k.owner == self) {
        ++k.depth;
        return;
    }
    double waitedMs = -1;
    if (k.depth != 0) {
        const auto start = std::chrono::steady_clock::now();
        k.released.wait(lock, [&k] { return k.depth == 0; });
        waitedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    k.owner = self;
    k.depth = 1;
    lock.unlock();
    if (waitedMs >= 0)
        noteWait(waitedMs);
}

void unlockKernelTo(int depth)
{
    auto& k = kernelMutex();
    const std::lock_guard lock(k.mutex);
    if (k.owner != std::this_thread::get_id() || depth >= k.depth)
        return;
    k.depth = std::max(depth, 0);
    if (k.depth == 0) {
        k.owner = std::thread::id();
        k.released.notify_all();
    }
}

void installKernelSignalHandlers()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // Let OCCT install its handlers, keep them for kernel calls and put
        // back what was there before. No floating-point traps: kernel code
        // relies on IEEE results.
#if defined(_WIN32)
        // OCCT's C signal handlers are replaced by onKernelSignal (their
        // mutex); its top-level exception filter stays (CrashLog chains to it).
        SignalHandler before[std::size(kKernelSignals)];
        for (std::size_t i = 0; i < std::size(kKernelSignals); ++i) {
            before[i] = std::signal(kKernelSignals[i], SIG_DFL);
            std::signal(kKernelSignals[i], before[i]);
        }
        OSD::SetSignal(OSD_SignalMode_Set, false);
        for (std::size_t i = 0; i < std::size(kKernelSignals); ++i)
            std::signal(kKernelSignals[i], before[i]);
#else
        struct sigaction before[std::size(kKernelSignals)];
        for (std::size_t i = 0; i < std::size(kKernelSignals); ++i)
            sigaction(kKernelSignals[i], nullptr, &before[i]);
        OSD::SetSignal(OSD_SignalMode_Set, false);
        for (std::size_t i = 0; i < std::size(kKernelSignals); ++i) {
            sigaction(kKernelSignals[i], &before[i], &g_kernelActions[i]);
            // The dispatcher blocks what OpenCASCADE's handler blocks.
            g_dispatchActions[i] = g_kernelActions[i];
            g_dispatchActions[i].sa_sigaction = onKernelSignal;
            g_dispatchActions[i].sa_flags = (g_kernelActions[i].sa_flags | SA_SIGINFO) & ~static_cast<int>(SA_RESETHAND);
        }
#endif
    });
}

KernelSignalScope::KernelSignalScope() : entryDepth_(kernelLockDepth())
{
    installKernelSignalHandlers();
    // One thread in the kernel at a time: then the handlers below are also
    // installed and removed by the thread that runs the kernel code (on
    // Windows the C runtime keeps them per thread).
    lockKernel();
    const std::lock_guard lock(g_signalMutex);
    if (g_kernelCalls++ > 0)
        return;
#if !defined(_WIN32)
    g_kernelThread.store(pthread_self());
    g_kernelThreadSet.store(true);
#endif
    for (std::size_t i = 0; i < std::size(kKernelSignals); ++i) {
#if defined(_WIN32)
        g_outsideHandlers[i] = std::signal(kKernelSignals[i], onKernelSignal);
#else
        sigaction(kKernelSignals[i], &g_dispatchActions[i], &g_outsideActions[i]);
#endif
    }
}

KernelSignalScope::~KernelSignalScope()
{
    {
        const std::lock_guard lock(g_signalMutex);
        if (--g_kernelCalls == 0) {
            for (std::size_t i = 0; i < std::size(kKernelSignals); ++i) {
#if defined(_WIN32)
                std::signal(kKernelSignals[i], g_outsideHandlers[i]);
#else
                sigaction(kKernelSignals[i], &g_outsideActions[i], nullptr);
#endif
            }
#if !defined(_WIN32)
            g_kernelThreadSet.store(false);
#endif
        }
    }
    // Also releases kernel locks taken inside this scope whose release a
    // kernel fault jumped over.
    unlockKernelTo(entryDepth_);
}

} // namespace detail

void installKernelSignalHandling()
{
    detail::installKernelSignalHandlers();
}

void setInteractiveThread()
{
    auto& w = detail::interactiveWaits();
    const std::lock_guard lock(w.mutex);
    w.thread = std::this_thread::get_id();
}

KernelWaits interactiveKernelWaits()
{
    auto& w = detail::interactiveWaits();
    const std::lock_guard lock(w.mutex);
    return w.waits;
}

void resetInteractiveKernelWaits()
{
    auto& w = detail::interactiveWaits();
    const std::lock_guard lock(w.mutex);
    w.waits = {};
}

std::uint64_t kernelCallsOnThisThread()
{
    return detail::t_kernelCalls;
}

bool insideKernelCall()
{
    return Standard_ErrorHandler::IsInTryBlock();
}

bool simulateKernelFault()
{
    const Status status = detail::guarded("simulated kernel fault", "The modeling kernel failed.", []() -> Status {
        int* volatile target = nullptr; // volatile: the compiler must not see it is null
        *target = 1;
        return okStatus();
    });
    return !status;
}

void runInsideKernelCallForTesting(const std::function<void()>& fn)
{
    (void)detail::guarded("test kernel call", "The modeling kernel failed.", [&fn]() -> Status {
        fn();
        return okStatus();
    });
}

namespace detail {

Result<Shape> finishSolid(const TopoDS_Shape& result, const char* operation, const char* userMessage)
{
    if (result.IsNull())
        return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, std::string(operation) + " returned a null shape");

    TopoDS_Shape out = result;
    int solidCount = 0;
    TopoDS_Shape lastSolid;
    for (TopExp_Explorer ex(result, TopAbs_SOLID); ex.More(); ex.Next()) {
        ++solidCount;
        lastSolid = ex.Current();
    }
    if (solidCount == 0)
        return Result<Shape>::failure(ErrorCode::EmptyResult, "This operation would remove the entire body.",
                                      std::string(operation) + " produced no solids");
    if (solidCount == 1)
        out = lastSolid;

    BRepCheck_Analyzer analyzer(out);
    if (!analyzer.IsValid()) {
        const std::string dev = std::string(operation) + " produced an invalid shape (BRepCheck_Analyzer failed)";
        OS_LOG(Error, Kernel) << dev;
        return Result<Shape>::failure(ErrorCode::InvalidResultShape, userMessage, dev);
    }

    std::vector<std::string> warnings;
    if (solidCount > 1)
        warnings.push_back("The body is now in " + std::to_string(solidCount) + " separate pieces.");
    return Result<Shape>::success(makeShape(out), std::move(warnings));
}

} // namespace detail

using namespace detail;

namespace {

bool validIndex(const Shape& shape, int index, int count)
{
    return !shape.isNull() && index >= 0 && index < count;
}

TopoDS_Face faceAt(const Shape& shape, int index)
{
    return TopoDS::Face(shape.data()->faces.FindKey(index + 1));
}

TopoDS_Edge edgeAt(const Shape& shape, int index)
{
    return TopoDS::Edge(shape.data()->edges.FindKey(index + 1));
}

// Outward normal of a face at (u,v), honoring face orientation.
std::optional<gp_Dir> faceNormal(const TopoDS_Face& face, double u, double v)
{
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
    if (surface.IsNull())
        return std::nullopt;
    GeomLProp_SLProps props(surface, u, v, 1, 1e-7);
    if (!props.IsNormalDefined())
        return std::nullopt;
    gp_Dir n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    return n;
}

SurfaceKind surfaceKind(GeomAbs_SurfaceType type)
{
    switch (type) {
    case GeomAbs_Plane: return SurfaceKind::Plane;
    case GeomAbs_Cylinder: return SurfaceKind::Cylinder;
    case GeomAbs_Cone: return SurfaceKind::Cone;
    case GeomAbs_Sphere: return SurfaceKind::Sphere;
    case GeomAbs_Torus: return SurfaceKind::Torus;
    case GeomAbs_BSplineSurface: return SurfaceKind::BSpline;
    default: return SurfaceKind::Other;
    }
}

CurveKind curveKind(GeomAbs_CurveType type)
{
    switch (type) {
    case GeomAbs_Line: return CurveKind::Line;
    case GeomAbs_Circle: return CurveKind::Circle;
    case GeomAbs_Ellipse: return CurveKind::Ellipse;
    case GeomAbs_BSplineCurve: return CurveKind::BSpline;
    default: return CurveKind::Other;
    }
}

// Runs a boolean leaving its inputs untouched. By default OCCT may raise
// tolerances of the arguments' sub-shapes in place; those belong to cached
// step outputs (and the previous state kept for undo), which must not change
// under later steps. Found by the undo/redo stress test: the same step
// recomputed after an undo gave a bounding box 4e-5 mm different.
template <typename Op>
void runBoolean(Op& op, const TopoDS_Shape& argument, const TopoDS_Shape& tool)
{
    TopTools_ListOfShape arguments, tools;
    arguments.Append(argument);
    tools.Append(tool);
    op.SetArguments(arguments);
    op.SetTools(tools);
    op.SetNonDestructive(Standard_True);
    op.Build();
}

} // namespace

Result<Shape> makeBox(const Vec3& origin, const Vec3& size)
{
    if (size.x < kMinLength || size.y < kMinLength || size.z < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Box dimensions must be greater than zero.",
                                      "makeBox with non-positive size");
    return guarded("BRepPrimAPI_MakeBox", "Unable to create the box.", [&] {
        BRepPrimAPI_MakeBox maker(toPnt(origin), size.x, size.y, size.z);
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, "Unable to create the box.", "BRepPrimAPI_MakeBox not done");
        return finishSolid(maker.Shape(), "BRepPrimAPI_MakeBox", "Unable to create the box.");
    });
}

Result<Shape> makeCylinder(const Vec3& baseCenter, const Vec3& axis, double radius, double height)
{
    if (radius < kMinLength || height < kMinLength || axis.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Cylinder radius and height must be greater than zero.",
                                      "makeCylinder with invalid arguments");
    return guarded("BRepPrimAPI_MakeCylinder", "Unable to create the cylinder.", [&] {
        const Vec3 a = axis.normalized();
        BRepPrimAPI_MakeCylinder maker(gp_Ax2(toPnt(baseCenter), gp_Dir(a.x, a.y, a.z)), radius, height);
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, "Unable to create the cylinder.", "BRepPrimAPI_MakeCylinder not done");
        return finishSolid(maker.Shape(), "BRepPrimAPI_MakeCylinder", "Unable to create the cylinder.");
    });
}

Result<Shape> pushPullFace(const Shape& shape, int faceIndex, double distance)
{
    if (!validIndex(shape, faceIndex, shape.faceCount()))
        return Result<Shape>::failure(ErrorCode::InvalidReference, "The selected face no longer exists.",
                                      "pushPullFace: face index " + std::to_string(faceIndex) + " out of range");
    if (std::abs(distance) < kMinLength)
        return Result<Shape>::success(shape); // zero offset: no change

    const char* userMessage = "Unable to move this face by that distance. Try a slightly different distance.";
    return guarded("pushPullFace", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("pushPullFace");
        const TopoDS_Face face = faceAt(shape, faceIndex);
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            return Result<Shape>::failure(ErrorCode::NotPlanar, "Only flat faces can be pushed or pulled for now.",
                                          "pushPullFace: face " + std::to_string(faceIndex) + " is not planar");
        const auto normal = faceNormal(face, (surface.FirstUParameter() + surface.LastUParameter()) / 2,
                                       (surface.FirstVParameter() + surface.LastVParameter()) / 2);
        if (!normal)
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "pushPullFace: normal undefined");

        const gp_Vec sweep = gp_Vec(*normal) * distance;
        BRepPrimAPI_MakePrism prismMaker(face, sweep);
        prismMaker.Build();
        if (!prismMaker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "BRepPrimAPI_MakePrism not done");
        TopoDS_Shape prism = prismMaker.Shape();
        // A face swept against its own normal yields an inside-out solid; fix
        // orientation so the boolean sees a proper closed solid.
        for (TopExp_Explorer ex(prism, TopAbs_SOLID); ex.More(); ex.Next()) {
            TopoDS_Solid solid = TopoDS::Solid(ex.Current());
            BRepLib::OrientClosedSolid(solid);
            prism = solid;
            break;
        }

        TopoDS_Shape combined;
        if (distance > 0) {
            BRepAlgoAPI_Fuse op;
            runBoolean(op, occ(shape), prism);
            if (op.HasErrors())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Fuse failed: " + describeAlgoErrors(op));
            combined = op.Shape();
        } else {
            BRepAlgoAPI_Cut op;
            runBoolean(op, occ(shape), prism);
            if (op.HasErrors())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Cut failed: " + describeAlgoErrors(op));
            combined = op.Shape();
        }

        ShapeUpgrade_UnifySameDomain unify(combined, true, true, true);
        unify.Build();
        return finishSolid(unify.Shape(), "pushPullFace", userMessage);
    });
}

namespace detail {

std::optional<double> largestWorkingSize(double failed, std::chrono::steady_clock::duration firstAttempt,
                                         const std::function<bool(double)>& works)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    Clock::duration slowest = firstAttempt;
    int attempts = 0;
    const auto affordable = [&] {
        return attempts < kMaxSizeAttempts && Clock::now() - start + slowest <= kSizeBudget;
    };
    const auto attempt = [&](double size) {
        const auto t0 = Clock::now();
        const bool ok = works(size);
        slowest = std::max(slowest, Clock::now() - t0);
        ++attempts;
        return ok;
    };

    std::optional<double> largest;
    double lo = std::min(kTinySize, failed * 0.02);
    double hi = failed;
    if (affordable()) {
        if (!attempt(lo)) {
            largest = 0.0;
        } else {
            while (hi / lo > 1.02 && affordable()) {
                const double mid = hi / lo > 2 ? std::sqrt(lo * hi) : (lo + hi) / 2;
                (attempt(mid) ? lo : hi) = mid;
            }
            if (hi / lo <= 1.5)
                largest = lo;
        }
    }
    OS_LOG(Debug, Geometry) << "largestWorkingSize: " << attempts << " attempts, "
                            << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << " ms, "
                            << (largest ? std::to_string(*largest) : std::string("unknown"));
    return largest;
}

} // namespace detail

namespace {

// ---- Plain-language hints for sizes the kernel refuses --------------------------

enum class SizedOperation { Fillet, Chamfer, Shell };

using Clock = std::chrono::steady_clock;

// detail::largestWorkingSize for one operation on one shape and items. The
// last answer is cached, "unknown" included: a drag keeps asking about the
// same shape and items, and should not wait for the search again.
std::optional<double> largestWorkingSize(SizedOperation operation, const Shape& shape, const std::vector<int>& items,
                                         double failed, Clock::duration firstAttempt,
                                         const std::function<bool(double)>& works)
{
    struct Entry {
        SizedOperation operation;
        Shape shape; // held, so its address cannot be reused by another shape
        std::vector<int> items;
        std::optional<double> largest;
    };
    static std::mutex mutex;
    static std::optional<Entry> last;
    {
        std::lock_guard lock(mutex);
        if (last && last->operation == operation && last->shape.sameAs(shape) && last->items == items
            && (!last->largest || *last->largest < failed))
            return last->largest;
    }
    const std::optional<double> largest = detail::largestWorkingSize(failed, firstAttempt, works);
    std::lock_guard lock(mutex);
    last = Entry{operation, shape, items, largest};
    return largest;
}

// "2.9 mm", "0.11 in": two significant digits (half units from 10 up),
// rounded down so the suggested value itself works.
std::string sizeText(double millimeters, LengthUnit unit)
{
    const double value = fromMillimeters(millimeters, unit);
    const double step = value >= 10 ? 0.5 : std::pow(10.0, std::floor(std::log10(value)) - 1);
    const double down = std::floor(value / step + 1e-9) * step;
    const int decimals = std::max(1, static_cast<int>(std::lround(-std::log10(step))));
    char text[48];
    std::snprintf(text, sizeof text, "%.*f %s", decimals, down, std::string(unitSymbol(unit)).c_str());
    return text;
}

// True when the two faces along the edge meet without a crease (a tangent
// edge, e.g. between a fillet and its neighbour): there is no corner there.
bool isSmoothEdge(const Shape& shape, int edgeIndex)
{
    const std::vector<int> faces = facesOfEdge(shape, edgeIndex);
    if (faces.size() != 2)
        return false;
    const TopoDS_Edge edge = edgeAt(shape, edgeIndex);
    std::optional<gp_Dir> normals[2];
    for (int k = 0; k < 2; ++k) {
        const TopoDS_Face face = faceAt(shape, faces[std::size_t(k)]);
        double first = 0, last = 0;
        const Handle(Geom2d_Curve) onFace = BRep_Tool::CurveOnSurface(edge, face, first, last);
        if (onFace.IsNull())
            return false;
        const gp_Pnt2d uv = onFace->Value((first + last) / 2);
        normals[k] = faceNormal(face, uv.X(), uv.Y());
        if (!normals[k])
            return false;
    }
    return normals[0]->Angle(*normals[1]) < 1.0 * kPi / 180;
}

// The message for an edge fillet or chamfer the kernel refused (the failed
// attempt took `attempt`).
std::string edgeSizeMessage(SizedOperation operation, const Shape& shape, const std::vector<int>& edges, double size,
                            const SizeAdvice& advice, Clock::duration attempt, const std::function<bool(double)>& works)
{
    const bool fillet = operation == SizedOperation::Fillet;
    const bool several = edges.size() > 1;
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        for (int e : edges)
            if (isSmoothEdge(shape, e))
                return std::string(several ? "One of these edges" : "This edge") + " joins two faces smoothly, so there is no corner to "
                     + (fillet ? "round" : "bevel") + ". Select sharp edges only.";
        const auto largest = advice.suggest ? largestWorkingSize(operation, shape, edges, size, attempt, works) : std::nullopt;
        const std::string what = fillet ? "radius" : "distance";
        const std::string where = several ? "these edges" : "this edge";
        if (largest && *largest <= 0)
            return std::string("Unable to ") + (fillet ? "round " : "bevel ") + where
                 + " at any size. Try fewer edges at a time, or remove nearby rounded edges first.";
        if (largest && *largest >= kMinLength)
            return "The " + what + " is too large for " + where + ". Try " + sizeText(*largest, advice.unit) + " or less.";
    } catch (const Standard_Failure&) {
    }
    return fillet ? "Unable to create this fillet. Try a smaller radius." : "Unable to create this chamfer. Try a smaller distance.";
}

Result<Shape> tryFillet(const Shape& shape, const std::vector<int>& edgeIndices, double radius)
{
    const char* userMessage = "Unable to create this fillet. Try a smaller radius.";
    return guarded("BRepFilletAPI_MakeFillet", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("filletEdges");
        BRepFilletAPI_MakeFillet maker(occ(shape));
        for (int e : edgeIndices)
            maker.Add(radius, edgeAt(shape, e));
        maker.Build();
        if (!maker.IsDone()) {
            std::ostringstream dev;
            dev << "BRepFilletAPI_MakeFillet not done: radius=" << radius << " mm, edges=" << edgeIndices.size()
                << ", faulty contours=" << maker.NbFaultyContours() << ", faulty vertices=" << maker.NbFaultyVertices();
            return Result<Shape>::failure(ErrorCode::FilletRadiusTooLarge, userMessage, dev.str());
        }
        return finishSolid(maker.Shape(), "BRepFilletAPI_MakeFillet", userMessage);
    });
}

Result<Shape> tryChamfer(const Shape& shape, const std::vector<int>& edgeIndices, double distance)
{
    const char* userMessage = "Unable to create this chamfer. Try a smaller distance.";
    return guarded("BRepFilletAPI_MakeChamfer", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("chamferEdges");
        BRepFilletAPI_MakeChamfer maker(occ(shape));
        for (int e : edgeIndices)
            maker.Add(distance, edgeAt(shape, e));
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::ChamferTooLarge, userMessage,
                                          "BRepFilletAPI_MakeChamfer not done: distance=" + std::to_string(distance));
        return finishSolid(maker.Shape(), "BRepFilletAPI_MakeChamfer", userMessage);
    });
}

Result<Shape> tryShell(const Shape& shape, const std::vector<int>& openFaces, double thickness)
{
    const char* userMessage = "Unable to shell with this wall thickness. Try thinner walls.";
    return guarded("BRepOffsetAPI_MakeThickSolid", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("shell");
        TopTools_ListOfShape faces;
        for (int f : openFaces)
            faces.Append(faceAt(shape, f));
        BRepOffsetAPI_MakeThickSolid maker;
        // Negative offset: walls grow inward, the outside stays where it is.
        maker.MakeThickSolidByJoin(occ(shape), faces, -thickness, 1e-3);
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::ShellTooThick, userMessage,
                                          "MakeThickSolidByJoin not done: thickness=" + std::to_string(thickness));
        auto hollow = finishSolid(maker.Shape(), "BRepOffsetAPI_MakeThickSolid", userMessage);
        // When the walls would meet, OCCT can report success and hand back the
        // untouched solid. A shell always removes material: reject anything else.
        if (hollow && volume(hollow.value()) >= volume(shape) * (1.0 - 1e-9))
            return Result<Shape>::failure(ErrorCode::ShellTooThick, userMessage,
                                          "MakeThickSolidByJoin removed no material: thickness=" + std::to_string(thickness));
        return hollow;
    });
}

} // namespace

Result<Shape> filletEdges(const Shape& shape, const std::vector<int>& edgeIndices, double radius, const SizeAdvice& advice)
{
    if (edgeIndices.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select at least one edge to fillet.", "filletEdges: no edges");
    if (radius < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The fillet radius must be greater than zero.",
                                      "filletEdges: radius " + std::to_string(radius));
    for (int e : edgeIndices)
        if (!validIndex(shape, e, shape.edgeCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected edge no longer exists.",
                                          "filletEdges: edge index " + std::to_string(e) + " out of range");
    const auto start = Clock::now();
    auto result = tryFillet(shape, edgeIndices, radius);
    if (result)
        return result;
    const std::string message = edgeSizeMessage(SizedOperation::Fillet, shape, edgeIndices, radius, advice, Clock::now() - start,
                                                [&](double r) { return tryFillet(shape, edgeIndices, r).ok(); });
    return Result<Shape>::failure(ErrorCode::FilletRadiusTooLarge, message, result.developerMessage());
}

Result<Shape> chamferEdges(const Shape& shape, const std::vector<int>& edgeIndices, double distance, const SizeAdvice& advice)
{
    if (edgeIndices.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select at least one edge to chamfer.", "chamferEdges: no edges");
    if (distance < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The chamfer distance must be greater than zero.",
                                      "chamferEdges: distance " + std::to_string(distance));
    for (int e : edgeIndices)
        if (!validIndex(shape, e, shape.edgeCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected edge no longer exists.",
                                          "chamferEdges: edge index " + std::to_string(e) + " out of range");
    const auto start = Clock::now();
    auto result = tryChamfer(shape, edgeIndices, distance);
    if (result)
        return result;
    const std::string message = edgeSizeMessage(SizedOperation::Chamfer, shape, edgeIndices, distance, advice, Clock::now() - start,
                                                [&](double d) { return tryChamfer(shape, edgeIndices, d).ok(); });
    return Result<Shape>::failure(ErrorCode::ChamferTooLarge, message, result.developerMessage());
}

Result<Shape> shell(const Shape& shape, const std::vector<int>& openFaces, double thickness, const SizeAdvice& advice)
{
    if (openFaces.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select the face(s) to open.", "shell: no faces");
    if (thickness < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The wall thickness must be greater than zero.",
                                      "shell: thickness " + std::to_string(thickness));
    for (int f : openFaces)
        if (!validIndex(shape, f, shape.faceCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected face no longer exists.",
                                          "shell: face index " + std::to_string(f) + " out of range");
    const auto start = Clock::now();
    auto result = tryShell(shape, openFaces, thickness);
    if (result)
        return result;
    std::string message = "Unable to shell with this wall thickness. Try thinner walls.";
    const auto largest = advice.suggest ? largestWorkingSize(SizedOperation::Shell, shape, openFaces, thickness, Clock::now() - start,
                                                             [&](double t) { return tryShell(shape, openFaces, t).ok(); })
                                        : std::nullopt;
    if (largest && *largest <= 0)
        message = "Unable to hollow this body with these faces open. Try opening a different face.";
    else if (largest && *largest >= kMinLength)
        message = "The walls are too thick for this body. Try " + sizeText(*largest, advice.unit) + " or less.";
    return Result<Shape>::failure(ErrorCode::ShellTooThick, message, result.developerMessage());
}

Result<Shape> booleanOp(const Shape& a, const Shape& b, BooleanKind kind)
{
    if (a.isNull() || b.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select two bodies.", "booleanOp: null operand");
    const char* userMessage = "Unable to combine these bodies. Moving one of them slightly often helps.";
    return guarded("booleanOp", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("booleanOp");
        TopoDS_Shape out;
        std::string errors;
        switch (kind) {
        case BooleanKind::Union: {
            BRepAlgoAPI_Fuse op;
            runBoolean(op, occ(a), occ(b));
            if (op.HasErrors())
                errors = describeAlgoErrors(op);
            else
                out = op.Shape();
            break;
        }
        case BooleanKind::Subtract: {
            BRepAlgoAPI_Cut op;
            runBoolean(op, occ(a), occ(b));
            if (op.HasErrors())
                errors = describeAlgoErrors(op);
            else
                out = op.Shape();
            break;
        }
        case BooleanKind::Intersect: {
            BRepAlgoAPI_Common op;
            runBoolean(op, occ(a), occ(b));
            if (op.HasErrors())
                errors = describeAlgoErrors(op);
            else
                out = op.Shape();
            break;
        }
        }
        if (!errors.empty())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Boolean failed: " + errors);
        if (kind == BooleanKind::Intersect && !TopExp_Explorer(out, TopAbs_SOLID).More())
            return Result<Shape>::failure(ErrorCode::EmptyResult, "These bodies do not overlap, so nothing would be left.",
                                          "booleanOp: intersection is empty");
        if (kind != BooleanKind::Intersect) {
            ShapeUpgrade_UnifySameDomain unify(out, true, true, true);
            unify.Build();
            out = unify.Shape();
        }
        auto result = finishSolid(out, "booleanOp", userMessage);
        // A cut must remove material. One that misses (or only touches the
        // surface) would silently change nothing.
        if (result && kind == BooleanKind::Subtract) {
            const double before = volume(a), after = volume(result.value());
            if (after >= before - 1e-9 * std::max(before, 1.0))
                return Result<Shape>::failure(ErrorCode::NoEffect, "The shapes do not overlap, so nothing would be cut away.",
                                              "booleanOp: subtraction removed no volume");
        }
        return result;
    });
}

Result<Shape> translated(const Shape& shape, const Vec3& offset)
{
    if (shape.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to move.", "translated: null shape");
    return guarded("translate", "Unable to move the body.", [&] {
        gp_Trsf trsf;
        trsf.SetTranslation(toVec(offset));
        BRepBuilderAPI_Transform op(occ(shape), trsf, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

Result<Shape> rotated(const Shape& shape, const Vec3& axisOrigin, const Vec3& axisDirection, double angleRadians)
{
    if (shape.isNull() || axisDirection.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to rotate.", "rotated: invalid input");
    return guarded("rotate", "Unable to rotate the body.", [&] {
        const Vec3 d = axisDirection.normalized();
        gp_Trsf trsf;
        trsf.SetRotation(gp_Ax1(toPnt(axisOrigin), gp_Dir(d.x, d.y, d.z)), angleRadians);
        BRepBuilderAPI_Transform op(occ(shape), trsf, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

namespace {
// Rodrigues rotation of v about unit axis k by angle a.
Vec3 rotateVector(const Vec3& v, const Vec3& k, double a)
{
    return v * std::cos(a) + k.cross(v) * std::sin(a) + k * (k.dot(v) * (1 - std::cos(a)));
}
} // namespace

Vec3 RigidMotion::apply(const Vec3& p) const
{
    const Vec3 k = axis.length() > 1e-12 ? axis.normalized() : Vec3{0, 0, 1};
    return center + rotateVector(p - center, k, angle) + translation;
}

Result<Shape> transformed(const Shape& shape, const RigidMotion& motion)
{
    if (shape.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to move.", "transformed: null shape");
    if (motion.isIdentity())
        return Result<Shape>::success(shape);
    if (std::abs(motion.angle) > 1e-12 && motion.axis.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Unable to rotate the body.", "transformed: zero axis");
    return guarded("transform", "Unable to move the body.", [&] {
        gp_Trsf rotation;
        if (std::abs(motion.angle) > 1e-12) {
            const Vec3 d = motion.axis.normalized();
            rotation.SetRotation(gp_Ax1(toPnt(motion.center), gp_Dir(d.x, d.y, d.z)), motion.angle);
        }
        gp_Trsf translation;
        translation.SetTranslation(toVec(motion.translation));
        BRepBuilderAPI_Transform op(occ(shape), translation * rotation, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

Result<Shape> deleteFaces(const Shape& shape, const std::vector<int>& faceIndices)
{
    if (faceIndices.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select the faces to remove.", "deleteFaces: no faces");
    for (int f : faceIndices)
        if (!validIndex(shape, f, shape.faceCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected face no longer exists.",
                                          "deleteFaces: face index " + std::to_string(f) + " out of range");
    const char* userMessage = "Unable to remove this. Its neighbours cannot close the gap.";
    return guarded("deleteFaces", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("deleteFaces");
        BRepAlgoAPI_Defeaturing defeaturing;
        defeaturing.SetShape(occ(shape));
        for (int f : faceIndices)
            defeaturing.AddFaceToRemove(faceAt(shape, f));
        defeaturing.Build();
        if (!defeaturing.IsDone() || defeaturing.HasErrors())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage,
                                          "Defeaturing failed: " + describeAlgoErrors(defeaturing));
        ShapeUpgrade_UnifySameDomain unify(defeaturing.Shape(), true, true, true);
        unify.Build();
        auto out = finishSolid(unify.Shape(), "deleteFaces", userMessage);
        // Defeaturing may leave faces it cannot remove in place and report
        // success: require a real change.
        if (out && out.value().faceCount() >= shape.faceCount()
            && std::abs(volume(out.value()) - volume(shape)) < 1e-9 * std::max(volume(shape), 1.0))
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "deleteFaces: shape unchanged");
        return out;
    });
}

Result<Shape> offsetFace(const Shape& shape, int faceIndex, double distance)
{
    if (!validIndex(shape, faceIndex, shape.faceCount()))
        return Result<Shape>::failure(ErrorCode::InvalidReference, "The selected face no longer exists.",
                                      "offsetFace: face index " + std::to_string(faceIndex) + " out of range");
    if (std::abs(distance) < kMinLength)
        return Result<Shape>::success(shape);
    const char* userMessage = "Unable to move this face that far. Its neighbours cannot follow.";
    return guarded("offsetFace", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("offsetFace");
        const TopoDS_Face face = faceAt(shape, faceIndex);
        BRepOffset_MakeOffset maker;
        maker.Initialize(occ(shape), 0.0, 1e-6, BRepOffset_Skin, false, false, GeomAbs_Intersection, false);
        maker.SetOffsetOnFace(face, distance);
        maker.MakeOffsetShape();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage,
                                          "BRepOffset_MakeOffset not done, error " + std::to_string(int(maker.Error())));
        // Skin mode yields a shell: close it into a solid.
        TopoDS_Shape result = maker.Shape();
        if (!TopExp_Explorer(result, TopAbs_SOLID).More()) {
            BRepBuilderAPI_MakeSolid solidMaker;
            for (TopExp_Explorer ex(result, TopAbs_SHELL); ex.More(); ex.Next())
                solidMaker.Add(TopoDS::Shell(ex.Current()));
            if (!solidMaker.IsDone())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "offsetFace: no closed shell");
            TopoDS_Solid solid = solidMaker.Solid();
            BRepLib::OrientClosedSolid(solid);
            result = solid;
        }
        ShapeUpgrade_UnifySameDomain unify(result, true, true, true);
        unify.Build();
        auto out = finishSolid(unify.Shape(), "offsetFace", userMessage);
        if (!out)
            return out;
        // Kernel "success" is not success: when the neighbours cannot follow
        // (tangent fillets), the solid is wrong. The volume must change by
        // about the face's area times the distance.
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        const double expected = props.Mass() * distance;
        const double actual = volume(out.value()) - volume(shape);
        if (!(volume(out.value()) > 0) || std::abs(actual - expected) > 0.25 * std::abs(expected) + 1e-9 * volume(shape))
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage,
                                          "offsetFace: volume changed by " + std::to_string(actual) + ", expected about "
                                              + std::to_string(expected));
        return out;
    });
}

Result<Shape> mirrored(const Shape& shape, const Vec3& planeOrigin, const Vec3& planeNormal)
{
    if (shape.isNull() || planeNormal.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to mirror.", "mirrored: invalid input");
    return guarded("mirror", "Unable to mirror the body.", [&] {
        const Vec3 n = planeNormal.normalized();
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2(toPnt(planeOrigin), gp_Dir(n.x, n.y, n.z)));
        // BRepBuilderAPI_Transform keeps solids valid under a reflection.
        BRepBuilderAPI_Transform op(occ(shape), mirror, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

namespace {
// Fuses `shapes` (at least one) in one General Fuse run, then merges faces
// that ended up on the same surface and validates the result.
Result<Shape> fuseInOnePass(const std::vector<TopoDS_Shape>& shapes, const char* operation, const char* userMessage)
{
    if (shapes.size() == 1)
        return finishSolid(shapes.front(), operation, userMessage);
    TopTools_ListOfShape arguments, tools;
    arguments.Append(shapes.front());
    for (std::size_t i = 1; i < shapes.size(); ++i)
        tools.Append(shapes[i]);
    BRepAlgoAPI_Fuse fuse;
    fuse.SetArguments(arguments);
    fuse.SetTools(tools);
    fuse.SetNonDestructive(Standard_True);
    fuse.Build();
    if (fuse.HasErrors())
        return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage,
                                      std::string(operation) + ": fuse failed: " + describeAlgoErrors(fuse));
    ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), true, true, true);
    unify.Build();
    return finishSolid(unify.Shape(), operation, userMessage);
}
} // namespace

Result<Shape> mirrorJoined(const Shape& shape, const Vec3& planeOrigin, const Vec3& planeNormal)
{
    auto image = mirrored(shape, planeOrigin, planeNormal);
    if (!image)
        return image;
    const char* userMessage = "Unable to mirror the body across this plane. Try another plane.";
    return guarded("mirrorJoined", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("mirrorJoined");
        auto joined = fuseInOnePass({occ(shape), occ(image.value())}, "mirrorJoined", userMessage);
        // The image of a body symmetric about the plane lands on itself.
        if (joined && volume(joined.value()) <= volume(shape) * (1 + 1e-9))
            return Result<Shape>::failure(ErrorCode::NoEffect,
                                          "The body is already symmetric about this plane, so mirroring would change nothing. "
                                          "Pick another plane.",
                                          "mirrorJoined: volume unchanged");
        return joined;
    });
}

Result<Shape> repeatJoined(const Shape& shape, const std::vector<RigidMotion>& copies)
{
    if (shape.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to repeat.", "repeatJoined: null shape");
    std::vector<TopoDS_Shape> parts{occ(shape)};
    for (const RigidMotion& motion : copies) {
        auto copy = transformed(shape, motion);
        if (!copy)
            return copy;
        parts.push_back(occ(copy.value()));
    }
    const char* userMessage = "Unable to repeat the body this way. Try a different spacing or number of copies.";
    return guarded("repeatJoined", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("repeatJoined");
        auto joined = fuseInOnePass(parts, "repeatJoined", userMessage);
        // Copies that all land on the original (no spacing, or a round body
        // turned about its own axis) add nothing.
        if (joined && !copies.empty() && volume(joined.value()) <= volume(shape) * (1 + 1e-9))
            return Result<Shape>::failure(ErrorCode::NoEffect,
                                          "The copies land on top of the original, so nothing would change. "
                                          "Use a larger spacing or a different axis.",
                                          "repeatJoined: volume unchanged");
        return joined;
    });
}

std::vector<Shape> solids(const Shape& shape)
{
    std::vector<Shape> out;
    if (shape.isNull())
        return out;
    const KernelLock lock;
    const auto& map = shape.data()->solids;
    for (int i = 1; i <= map.Extent(); ++i)
        out.push_back(makeShape(map(i)));
    return out;
}

SolidSignature solidSignature(const Shape& solid)
{
    SolidSignature signature;
    if (solid.isNull())
        return signature;
    const KernelLock lock;
    GProp_GProps props;
    BRepGProp::VolumeProperties(occ(solid), props);
    signature.volume = props.Mass();
    signature.centroid = fromPnt(props.CentreOfMass());
    const BoundingBox box = approximateBoundingBox(solid);
    signature.min = box.min;
    signature.max = box.max;
    return signature;
}

Result<Shape> gatherSolids(const std::vector<Shape>& pieces)
{
    const char* userMessage = "Unable to separate the pieces of this body.";
    if (pieces.empty())
        return Result<Shape>::failure(ErrorCode::EmptyResult, userMessage, "gatherSolids: no solids");
    return guarded("gatherSolids", userMessage, [&]() -> Result<Shape> {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const Shape& piece : pieces)
            if (!piece.isNull())
                builder.Add(compound, occ(piece));
        return finishSolid(compound, "gatherSolids", userMessage);
    });
}

Result<Shape> pushPullFaceKeepingEdges(const Shape& shape, int faceIndex, double distance)
{
    using R = Result<Shape>;
    auto notHere = [](const std::string& why) {
        return R::failure(ErrorCode::Unsupported, "This face cannot move together with its rounded edges.",
                          "pushPullFaceKeepingEdges: " + why);
    };
    const auto info = faceInfo(shape, faceIndex);
    if (!info || !info->isPlanar())
        return notHere("not a flat face");
    if (std::abs(distance) < kMinLength)
        return R::success(shape);
    const char* userMessage = "Unable to move this face by that distance. Try a slightly different distance.";
    return guarded("pushPullFaceKeepingEdges", userMessage, [&]() -> R {
        ScopedTimer timer("pushPullFaceKeepingEdges");
        const Vec3 n = info->normal.normalized();
        const double top = n.dot(info->planeOrigin);
        const int count = shape.faceCount();

        // Every face's extent along the normal: turn the part so the normal is
        // +Z, then read the faces' bounding boxes.
        gp_Trsf turn;
        turn.SetRotation(gp_Quaternion(gp_Vec(toDir(n)), gp_Vec(0, 0, 1)));
        const TopoDS_Shape turned = BRepBuilderAPI_Transform(occ(shape), turn, Standard_False).Shape();
        TopTools_IndexedMapOfShape turnedFaces;
        TopExp::MapShapes(turned, TopAbs_FACE, turnedFaces);
        if (turnedFaces.Extent() != count)
            return notHere("face order changed when turned");
        std::vector<double> low(static_cast<std::size_t>(count)), high(static_cast<std::size_t>(count));
        std::vector<char> wall(static_cast<std::size_t>(count), 0);
        for (int i = 0; i < count; ++i) {
            // The fast box follows the display mesh (the tight one costs tens
            // of ms per curved face); it errs a little outward, which is safe.
            Bnd_Box box;
            BRepBndLib::Add(turnedFaces(i + 1), box, Standard_True);
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            const double gap = box.GetGap();
            low[std::size_t(i)] = z0 + gap;
            high[std::size_t(i)] = z1 - gap;
            // Walls run along the normal: flat faces square to the face, and
            // cylinders (holes, shafts, rounded vertical edges) parallel to it.
            const BRepAdaptor_Surface surface(faceAt(shape, i));
            if (surface.GetType() == GeomAbs_Plane)
                wall[std::size_t(i)] = std::abs(fromDir(surface.Plane().Axis().Direction()).dot(n)) < 1e-7;
            else if (surface.GetType() == GeomAbs_Cylinder)
                wall[std::size_t(i)] = std::abs(std::abs(fromDir(surface.Cylinder().Axis().Direction()).dot(n)) - 1) < 1e-7;
        }
        auto isWall = [&](int i) { return wall[std::size_t(i)] != 0; };
        // The rounded or bevelled edges: neighbours of the face that are not walls.
        TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
        TopExp::MapShapesAndAncestors(occ(shape), TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        double deepest = top;
        bool treated = false;
        for (TopExp_Explorer e(faceAt(shape, faceIndex), TopAbs_EDGE); e.More(); e.Next()) {
            for (const TopoDS_Shape& f : edgeFaces.FindFromKey(e.Current())) {
                const int j = shape.data()->faces.FindIndex(f) - 1;
                if (j < 0 || j == faceIndex || isWall(j))
                    continue;
                treated = true;
                deepest = std::min(deepest, low[std::size_t(j)]);
            }
        }
        if (!treated)
            return notHere("no rounded or bevelled edges around the face");

        // Split just below them; everything the split passes through (and,
        // when pulling in, the slab taken out) must be a wall.
        const double split = deepest - std::max(0.05, 0.02 * (top - deepest));
        const double bandLow = distance < 0 ? split + distance : split;
        double bottom = top;
        for (int i = 0; i < count; ++i) {
            const auto k = std::size_t(i);
            bottom = std::min(bottom, low[k]);
            if (low[k] < split - 1e-7 && high[k] > bandLow + 1e-7 && !isWall(i))
                return notHere("face " + std::to_string(i) + " crosses the band that moves");
        }
        if (bottom > bandLow - 1e-6)
            return notHere("nothing below the split");

        // A box above a plane at height `h` (large enough to hold the part).
        const auto bounds = approximateBoundingBox(shape);
        const double size = 4 * std::max(bounds.size().length(), 1.0) + 2 * std::abs(distance);
        auto boxAbove = [&](double h) {
            const Vec3 c = bounds.center() - n * (n.dot(bounds.center()) - h);
            const gp_Ax2 frame(toPnt(c), toDir(n));
            const gp_Pnt corner = toPnt(c).Translated(gp_Vec(frame.XDirection()) * (-size / 2))
                                         .Translated(gp_Vec(frame.YDirection()) * (-size / 2));
            return BRepPrimAPI_MakeBox(gp_Ax2(corner, frame.Direction(), frame.XDirection()), size, size, size).Shape();
        };
        const TopoDS_Shape above = boxAbove(split);
        BRepAlgoAPI_Common upperOp;
        runBoolean(upperOp, occ(shape), above);
        BRepAlgoAPI_Cut lowerOp;
        runBoolean(lowerOp, occ(shape), distance > 0 ? above : boxAbove(bandLow));
        if (upperOp.HasErrors() || lowerOp.HasErrors())
            return R::failure(ErrorCode::KernelFailure, userMessage, "pushPullFaceKeepingEdges: split failed");
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(toDir(n)) * distance);
        const TopoDS_Shape upper = BRepBuilderAPI_Transform(upperOp.Shape(), shift, Standard_True).Shape();

        // The cross-section on the split plane (the cut faces of the lower part
        // facing the normal): extruded to fill the gap when pushing out, and
        // the measure for the volume check either way.
        std::vector<TopoDS_Shape> parts{lowerOp.Shape(), upper};
        double capArea = 0;
        const double capHeight = distance > 0 ? split : bandLow;
        for (TopExp_Explorer f(lowerOp.Shape(), TopAbs_FACE); f.More(); f.Next()) {
            const TopoDS_Face cap = TopoDS::Face(f.Current());
            BRepAdaptor_Surface surface(cap);
            if (surface.GetType() != GeomAbs_Plane)
                continue;
            const auto cn = faceNormal(cap, (surface.FirstUParameter() + surface.LastUParameter()) / 2,
                                       (surface.FirstVParameter() + surface.LastVParameter()) / 2);
            if (!cn || fromDir(*cn).dot(n) < 1 - 1e-9 || std::abs(n.dot(fromPnt(surface.Plane().Location())) - capHeight) > 1e-6)
                continue;
            GProp_GProps props;
            BRepGProp::SurfaceProperties(cap, props);
            capArea += props.Mass();
            if (distance > 0) {
                BRepPrimAPI_MakePrism prism(cap, gp_Vec(toDir(n)) * distance);
                prism.Build();
                if (!prism.IsDone())
                    return R::failure(ErrorCode::KernelFailure, userMessage, "pushPullFaceKeepingEdges: prism failed");
                parts.push_back(prism.Shape());
            }
        }
        if (capArea < 1e-9)
            return notHere("no cross-section at the split");
        auto result = fuseInOnePass(parts, "pushPullFaceKeepingEdges", userMessage);
        if (!result)
            return result;
        // The part must grow or shrink by exactly the cross-section times the distance.
        const double expected = capArea * distance, change = volume(result.value()) - volume(shape);
        if (std::abs(change - expected) > 1e-6 * std::max(1.0, std::abs(expected)) + 1e-6)
            return notHere("volume changed by " + std::to_string(change) + " instead of " + std::to_string(expected));
        return result;
    });
}

std::optional<AlignFrame> alignFrame(const Shape& shape, SubShapeKind kind, int index)
{
    if (kind == SubShapeKind::Face) {
        const auto info = faceInfo(shape, index);
        if (!info)
            return std::nullopt;
        if (info->isPlanar())
            return AlignFrame{info->centroid, info->normal.normalized(), true};
        if (info->hasAxis() && info->axisDirection.length() > 0.5)
            return AlignFrame{info->axisOrigin, info->axisDirection.normalized(), false};
        return std::nullopt;
    }
    if (kind == SubShapeKind::Edge) {
        const auto info = edgeInfo(shape, index);
        if (!info)
            return std::nullopt;
        if (info->kind == CurveKind::Line)
            return AlignFrame{info->midpoint, info->tangent.normalized(), false};
        if (info->kind == CurveKind::Circle && info->axis.length() > 0.5)
            return AlignFrame{info->center, info->axis.normalized(), false};
    }
    return std::nullopt;
}

RigidMotion alignMotion(const AlignFrame& source, const AlignFrame& target, bool flip, double offset)
{
    const Vec3 s = source.direction.normalized();
    Vec3 t = target.direction.normalized();
    if (source.sided && target.sided)
        t = t * -1.0; // flat faces meet face to face
    else if (s.dot(t) < 0)
        t = t * -1.0; // unsided: the smaller turn
    if (flip)
        t = t * -1.0;

    RigidMotion motion;
    motion.center = source.point;
    const Vec3 cross = s.cross(t);
    const double sine = cross.length(), cosine = std::clamp(s.dot(t), -1.0, 1.0);
    if (sine > 1e-9) {
        motion.axis = cross * (1.0 / sine);
        motion.angle = std::atan2(sine, cosine);
    } else if (cosine < 0) {
        // Opposite directions: half a turn about any axis perpendicular to s.
        const Vec3 helper = std::abs(s.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        motion.axis = s.cross(helper).normalized();
        motion.angle = kPi;
    }
    motion.translation = target.point + target.direction.normalized() * offset - source.point;
    return motion;
}

double volume(const Shape& shape)
{
    if (shape.isNull())
        return 0.0;
    const KernelLock lock;
    GProp_GProps props;
    BRepGProp::VolumeProperties(occ(shape), props);
    return props.Mass();
}

double surfaceArea(const Shape& shape)
{
    if (shape.isNull())
        return 0.0;
    const KernelLock lock;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(occ(shape), props);
    return props.Mass();
}

namespace {
BoundingBox toBoundingBox(const Bnd_Box& box)
{
    BoundingBox out;
    if (box.IsVoid())
        return out;
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    out.min = {x0, y0, z0};
    out.max = {x1, y1, z1};
    out.valid = true;
    return out;
}
} // namespace

BoundingBox boundingBox(const Shape& shape)
{
    if (shape.isNull())
        return {};
    // AddOptimal optimizes over every face and edge (~40 ms for a filleted
    // cube), so it runs once per shape; callers ask for the same shape often.
    // The kernel lock also guards the cached box (not a std::call_once: a
    // kernel fault jumping out of one would leave it blocked for good).
    const ShapeData& data = *shape.data();
    const KernelLock lock;
    if (!data.tightBoxDone) {
        Bnd_Box box;
        BRepBndLib::AddOptimal(data.shape, box, false, false);
        data.tightBox = toBoundingBox(box);
        data.tightBoxDone = true;
    }
    return data.tightBox;
}

BoundingBox approximateBoundingBox(const Shape& shape)
{
    if (shape.isNull())
        return {};
    // Geometry bounds (control-point hulls for B-splines), independent of any
    // triangulation, so the result does not depend on what was meshed before.
    const KernelLock lock;
    Bnd_Box box;
    BRepBndLib::Add(occ(shape), box, false);
    return toBoundingBox(box);
}

std::vector<int> facesChangedBy(const Shape& before, const Shape& after, const Shape& current)
{
    std::vector<int> out;
    if (after.isNull() || current.isNull())
        return out;
    const KernelLock lock;
    // TopTools_MapOfShape compares with IsSame (same TShape and location), so a
    // moved face counts as changed while an untouched one does not.
    TopTools_MapOfShape old;
    if (!before.isNull())
        for (int i = 1; i <= before.data()->faces.Extent(); ++i)
            old.Add(before.data()->faces(i));
    TopTools_MapOfShape changed;
    for (int i = 1; i <= after.data()->faces.Extent(); ++i)
        if (!old.Contains(after.data()->faces(i)))
            changed.Add(after.data()->faces(i));
    for (int i = 1; i <= current.data()->faces.Extent(); ++i)
        if (changed.Contains(current.data()->faces(i)))
            out.push_back(i - 1);
    return out;
}

std::vector<int> facesCreatedBy(const Shape& before, const Shape& after, const Shape& current)
{
    std::vector<int> out;
    if (after.isNull() || current.isNull())
        return out;
    const KernelLock lock;
    // Trimmed or split faces are rebuilt on the input face's surface object;
    // genuinely new faces get new surfaces.
    std::set<const Geom_Surface*> oldSurfaces;
    TopTools_MapOfShape oldFaces;
    if (!before.isNull())
        for (int i = 1; i <= before.data()->faces.Extent(); ++i) {
            const TopoDS_Face face = TopoDS::Face(before.data()->faces(i));
            oldFaces.Add(face);
            TopLoc_Location location;
            oldSurfaces.insert(BRep_Tool::Surface(face, location).get());
        }
    TopTools_MapOfShape created;
    for (int i = 1; i <= after.data()->faces.Extent(); ++i) {
        const TopoDS_Face face = TopoDS::Face(after.data()->faces(i));
        TopLoc_Location location;
        if (!oldFaces.Contains(face) && !oldSurfaces.count(BRep_Tool::Surface(face, location).get()))
            created.Add(face);
    }
    for (int i = 1; i <= current.data()->faces.Extent(); ++i)
        if (created.Contains(current.data()->faces(i)))
            out.push_back(i - 1);
    return out;
}

bool isValid(const Shape& shape)
{
    if (shape.isNull())
        return false;
    const KernelLock lock;
    BRepCheck_Analyzer analyzer(occ(shape));
    return analyzer.IsValid();
}

std::optional<FaceInfo> faceInfo(const Shape& shape, int faceIndex)
{
    if (!validIndex(shape, faceIndex, shape.faceCount()))
        return std::nullopt;
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        const TopoDS_Face face = faceAt(shape, faceIndex);
        FaceInfo info;
        BRepAdaptor_Surface surface(face);
        info.kind = surfaceKind(surface.GetType());

        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        info.area = props.Mass();
        info.centroid = fromPnt(props.CentreOfMass());

        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        if (const auto n = faceNormal(face, (u0 + u1) / 2, (v0 + v1) / 2))
            info.normal = fromDir(*n);
        info.point = fromPnt(surface.Value((u0 + u1) / 2, (v0 + v1) / 2));
        if (info.kind == SurfaceKind::Plane)
            info.planeOrigin = fromPnt(surface.Plane().Location());
        if (info.hasAxis()) {
            const gp_Ax1 axis = info.kind == SurfaceKind::Cylinder ? surface.Cylinder().Axis() : surface.Cone().Axis();
            const Vec3 origin = fromPnt(axis.Location());
            const Vec3 direction = fromDir(axis.Direction());
            info.axisDirection = direction;
            info.axisOrigin = origin + direction * (info.centroid - origin).dot(direction);
            info.radius = info.kind == SurfaceKind::Cylinder ? surface.Cylinder().Radius() : surface.Cone().RefRadius();
        }
        return info;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "faceInfo(" << faceIndex << ") failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::optional<EdgeInfo> edgeInfo(const Shape& shape, int edgeIndex)
{
    if (!validIndex(shape, edgeIndex, shape.edgeCount()))
        return std::nullopt;
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        const TopoDS_Edge edge = edgeAt(shape, edgeIndex);
        if (BRep_Tool::Degenerated(edge))
            return std::nullopt;
        BRepAdaptor_Curve curve(edge);
        EdgeInfo info;
        info.kind = curveKind(curve.GetType());
        const double t0 = curve.FirstParameter();
        const double t1 = curve.LastParameter();
        info.start = fromPnt(curve.Value(t0));
        info.end = fromPnt(curve.Value(t1));
        gp_Pnt mid;
        gp_Vec tangent;
        curve.D1((t0 + t1) / 2, mid, tangent);
        info.midpoint = fromPnt(mid);
        info.tangent = fromVec(tangent).normalized();
        info.length = GCPnts_AbscissaPoint::Length(curve);
        if (info.kind == CurveKind::Circle) {
            const gp_Circ circle = curve.Circle();
            info.radius = circle.Radius();
            info.center = fromPnt(circle.Location());
            info.axis = fromDir(circle.Axis().Direction());
        }
        return info;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "edgeInfo(" << edgeIndex << ") failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::vector<int> facesOfEdge(const Shape& shape, int edgeIndex)
{
    std::vector<int> result;
    if (!validIndex(shape, edgeIndex, shape.edgeCount()))
        return result;
    const KernelLock lock;
    TopTools_IndexedDataMapOfShapeListOfShape map;
    TopExp::MapShapesAndAncestors(occ(shape), TopAbs_EDGE, TopAbs_FACE, map);
    const TopoDS_Shape& edge = shape.data()->edges.FindKey(edgeIndex + 1);
    if (!map.Contains(edge))
        return result;
    for (const TopoDS_Shape& face : map.FindFromKey(edge)) {
        const int index = shape.data()->faces.FindIndex(face);
        if (index > 0 && std::find(result.begin(), result.end(), index - 1) == result.end())
            result.push_back(index - 1);
    }
    return result;
}

std::optional<Vec3> pointOnFace(const Shape& shape, int faceIndex, const Vec3& preferred)
{
    if (!validIndex(shape, faceIndex, shape.faceCount()))
        return std::nullopt;
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        const TopoDS_Face face = faceAt(shape, faceIndex);
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            return std::nullopt;
        const double tolerance = BRep_Tool::Tolerance(face);
        auto inside = [&](double u, double v) {
            BRepClass_FaceClassifier classifier(face, gp_Pnt2d(u, v), tolerance);
            return classifier.State() == TopAbs_IN;
        };
        double un = 0, vn = 0;
        ElSLib::Parameters(surface.Plane(), toPnt(preferred), un, vn);
        if (inside(un, vn))
            return fromPnt(surface.Value(un, vn));

        // Sample the face's parameter box (plane parameters are lengths) and
        // take the sample farthest from any outside sample (the middle of a
        // washer's ring, not its rim), nearest to `preferred` among equals.
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        constexpr int n = 24;
        std::vector<std::pair<double, double>> in, out;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const double u = u0 + (u1 - u0) * (i + 0.5) / n, v = v0 + (v1 - v0) * (j + 0.5) / n;
                (inside(u, v) ? in : out).emplace_back(u, v);
            }
        if (in.empty())
            return std::nullopt;
        double bestClearance = -1, bestDistance = 0;
        std::pair<double, double> best = in.front();
        for (const auto& [u, v] : in) {
            // The parameter box bounds the face too (a plain rectangle has no
            // outside samples at all).
            double clearance = std::min({u - u0, u1 - u, v - v0, v1 - v});
            for (const auto& [ou, ov] : out)
                clearance = std::min(clearance, std::hypot(u - ou, v - ov));
            const double distance = std::hypot(u - un, v - vn);
            if (clearance > bestClearance + 1e-9 || (clearance > bestClearance - 1e-9 && distance < bestDistance)) {
                bestClearance = clearance;
                bestDistance = distance;
                best = {u, v};
            }
        }
        return fromPnt(surface.Value(best.first, best.second));
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "pointOnFace(" << faceIndex << ") failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::optional<FaceThickness> faceThickness(const Shape& shape, int faceIndex, const Vec3& point)
{
    const auto info = faceInfo(shape, faceIndex);
    if (!info || !info->isPlanar())
        return std::nullopt;
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        const Vec3 normal = info->normal.normalized();
        IntCurvesFace_ShapeIntersector intersector;
        intersector.Load(occ(shape), Precision::Confusion());
        intersector.Perform(gp_Lin(toPnt(point), toDir(-normal)), -Precision::Confusion(), Precision::Infinite());
        if (!intersector.IsDone())
            return std::nullopt;
        intersector.SortResult();
        for (int i = 1; i <= intersector.NbPnt(); ++i) {
            const int hit = shape.data()->faces.FindIndex(intersector.Face(i)) - 1;
            if (hit == faceIndex || intersector.WParameter(i) < Precision::Confusion())
                continue; // the face itself
            // The first other face the line reaches is where it leaves the material.
            const auto opposite = faceInfo(shape, hit);
            if (!opposite || !opposite->isPlanar() || opposite->normal.normalized().dot(normal) > -1 + 1e-9)
                return std::nullopt;
            FaceThickness result;
            result.distance = (point - opposite->planeOrigin).dot(normal);
            result.from = point;
            result.to = point - normal * result.distance;
            result.oppositeFace = hit;
            if (result.distance <= Precision::Confusion())
                return std::nullopt;
            return result;
        }
        return std::nullopt;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "faceThickness(" << faceIndex << ") failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::optional<Measurement> measure(const SubShapeRef& a, const SubShapeRef& b)
{
    auto resolve = [](const SubShapeRef& r) -> TopoDS_Shape {
        if (!r.shape || r.shape->isNull())
            return {};
        const ShapeData* data = r.shape->data();
        switch (r.kind) {
        case SubShapeKind::Face:
            return r.index >= 0 && r.index < data->faces.Extent() ? data->faces.FindKey(r.index + 1) : TopoDS_Shape();
        case SubShapeKind::Edge:
            return r.index >= 0 && r.index < data->edges.Extent() ? data->edges.FindKey(r.index + 1) : TopoDS_Shape();
        case SubShapeKind::Vertex:
            return r.index >= 0 && r.index < data->vertices.Extent() ? data->vertices.FindKey(r.index + 1) : TopoDS_Shape();
        case SubShapeKind::Whole:
            return data->shape;
        }
        return {};
    };
    const TopoDS_Shape sa = resolve(a), sb = resolve(b);
    if (sa.IsNull() || sb.IsNull())
        return std::nullopt;
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        BRepExtrema_DistShapeShape extrema(sa, sb);
        if (!extrema.IsDone() || extrema.NbSolution() < 1)
            return std::nullopt;
        Measurement m;
        m.distance = extrema.Value();
        m.pointA = fromPnt(extrema.PointOnShape1(1));
        m.pointB = fromPnt(extrema.PointOnShape2(1));
        // Angles and gaps for the common maker questions: wall thickness
        // (parallel faces) and whether two faces/edges are square.
        if (a.kind == SubShapeKind::Face && b.kind == SubShapeKind::Face) {
            const auto fa = faceInfo(*a.shape, a.index);
            const auto fb = faceInfo(*b.shape, b.index);
            if (fa && fb && fa->isPlanar() && fb->isPlanar()) {
                const double c = std::clamp(std::abs(fa->normal.dot(fb->normal)), 0.0, 1.0);
                m.angle = std::acos(c);
                if (c > 1.0 - 1e-9)
                    m.parallelGap = std::abs((fb->centroid - fa->centroid).dot(fa->normal));
            }
        } else if (a.kind == SubShapeKind::Edge && b.kind == SubShapeKind::Edge) {
            const auto ea = edgeInfo(*a.shape, a.index);
            const auto eb = edgeInfo(*b.shape, b.index);
            if (ea && eb && ea->kind == CurveKind::Line && eb->kind == CurveKind::Line)
                m.angle = std::acos(std::clamp(std::abs(ea->tangent.dot(eb->tangent)), 0.0, 1.0));
        }
        return m;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "measure failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::string toBrepString(const Shape& shape)
{
    if (shape.isNull())
        return {};
    const KernelLock lock; // also writes triangulations, which meshing changes
    std::ostringstream out;
    BRepTools::Write(occ(shape), out);
    return out.str();
}

Result<Shape> fromBrepString(const std::string& text)
{
    return guarded("BRepTools::Read", "The stored geometry could not be read.", [&]() -> Result<Shape> {
        std::istringstream in(text);
        TopoDS_Shape shape;
        BRep_Builder builder;
        BRepTools::Read(shape, in, builder);
        if (shape.IsNull())
            return Result<Shape>::failure(ErrorCode::FileFormatError, "The stored geometry could not be read.",
                                          "BRepTools::Read produced a null shape");
        return Result<Shape>::success(makeShape(shape));
    });
}

} // namespace os::geom
