// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Exchange.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>

#include <mutex>

namespace os::geom {

namespace {

// Interface_Static parameters are process-global; serialize STEP access.
std::mutex& stepMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::string pathString(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

TopoDS_Shape combine(const std::vector<NamedShape>& shapes)
{
    if (shapes.size() == 1)
        return occ(shapes.front().shape);
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const auto& s : shapes)
        if (!s.shape.isNull())
            builder.Add(compound, occ(s.shape));
    return compound;
}

} // namespace

Status exportStep(const std::vector<NamedShape>& shapes, const std::filesystem::path& path)
{
    if (shapes.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to export.", "exportStep: no shapes");
    ScopedTimer timer("exportStep");
    // The lock stays outside the try block: a kernel crash jumps back into it.
    std::lock_guard lock(stepMutex());
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        STEPControl_Writer writer;
        Interface_Static::SetCVal("write.step.unit", "MM");
        Interface_Static::SetCVal("write.step.schema", "AP214IS");
        for (const auto& s : shapes) {
            if (s.shape.isNull())
                continue;
            if (writer.Transfer(occ(s.shape), STEPControl_AsIs) != IFSelect_RetDone)
                return Status::failure(ErrorCode::FileWriteError, "Unable to export STEP.",
                                       "STEPControl_Writer::Transfer failed for '" + s.name + "'");
        }
        if (writer.Write(pathString(path).c_str()) != IFSelect_RetDone)
            return Status::failure(ErrorCode::FileWriteError, "Unable to write the STEP file. Check the location is writable.",
                                   "STEPControl_Writer::Write failed: " + pathString(path));
    } catch (const Standard_Failure& failure) {
        return Status::failure(ErrorCode::FileWriteError, "Unable to export STEP.",
                               std::string("exportStep threw ") + failure.DynamicType()->Name());
    }
    OS_LOG(Info, File) << "exported STEP " << pathString(path);
    return okStatus();
}

Result<std::vector<NamedShape>> importStep(const std::filesystem::path& path)
{
    using R = Result<std::vector<NamedShape>>;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return R::failure(ErrorCode::FileNotFound, "The file could not be found.", "importStep: not a file: " + pathString(path));
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxImportFileBytes)
        return R::failure(ErrorCode::FileReadError, "The file is too large to import.",
                          "importStep: size check failed for " + pathString(path));
    ScopedTimer timer("importStep");
    std::lock_guard lock(stepMutex());
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        STEPControl_Reader reader;
        if (reader.ReadFile(pathString(path).c_str()) != IFSelect_RetDone)
            return R::failure(ErrorCode::FileFormatError, "This does not look like a valid STEP file.",
                              "STEPControl_Reader::ReadFile failed: " + pathString(path));
        reader.TransferRoots();
        std::vector<NamedShape> out;
        const TopoDS_Shape all = reader.OneShape();
        int index = 1;
        for (TopExp_Explorer ex(all, TopAbs_SOLID); ex.More(); ex.Next())
            out.push_back({"Imported " + std::to_string(index++), makeShape(ex.Current())});
        if (out.empty())
            return R::failure(ErrorCode::FileFormatError, "The STEP file contains no solid bodies.",
                              "importStep: no solids in " + pathString(path));
        return R::success(std::move(out));
    } catch (const Standard_Failure& failure) {
        return R::failure(ErrorCode::FileFormatError, "The STEP file could not be read.",
                          std::string("importStep threw ") + failure.DynamicType()->Name());
    }
}

Status exportStl(const std::vector<NamedShape>& shapes, const std::filesystem::path& path, const StlOptions& options)
{
    if (shapes.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to export.", "exportStl: no shapes");
    ScopedTimer timer("exportStl");
    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        const TopoDS_Shape shape = combine(shapes);
        BRepMesh_IncrementalMesh mesher(shape, options.linearDeflection, false, options.angularDeflection, false);
        (void)mesher;
        StlAPI_Writer writer;
        writer.ASCIIMode() = !options.binary;
        if (!writer.Write(shape, pathString(path).c_str()))
            return Status::failure(ErrorCode::FileWriteError, "Unable to write the STL file. Check the location is writable.",
                                   "StlAPI_Writer::Write failed: " + pathString(path));
    } catch (const Standard_Failure& failure) {
        return Status::failure(ErrorCode::FileWriteError, "Unable to export STL.",
                               std::string("exportStl threw ") + failure.DynamicType()->Name());
    }
    OS_LOG(Info, File) << "exported STL " << pathString(path);
    return okStatus();
}

} // namespace os::geom
