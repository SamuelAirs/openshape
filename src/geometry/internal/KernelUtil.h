#pragma once

// Private to the geometry library: helpers shared by the kernel-facing
// implementation files. Do not include from other layers.

#include "core/Log.h"
#include "core/Result.h"
#include "geometry/Shape.h"

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

// Runs `fn` and converts any kernel exception into a failed Result.
template <typename Fn>
Result<Shape> guarded(const char* operation, const char* userMessage, Fn&& fn)
{
    try {
        return fn();
    } catch (const Standard_Failure& failure) {
        const std::string dev = std::string(operation) + " threw " + describeFailure(failure);
        OS_LOG(Error, Kernel) << dev;
        return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, dev);
    } catch (const std::exception& e) {
        const std::string dev = std::string(operation) + " threw std::exception: " + e.what();
        OS_LOG(Error, Kernel) << dev;
        return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, dev);
    }
}

// Normalizes a kernel result: unwraps single-solid compounds, rejects empty
// results, and runs the B-rep validity checker.
Result<Shape> finishSolid(const TopoDS_Shape& result, const char* operation, const char* userMessage);

inline gp_Dir toDir(const Vec3& v) { return gp_Dir(v.x, v.y, v.z); }

} // namespace os::geom::detail
