// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Exchange.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/internal/ShapeData.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <DESTEP_Parameters.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Curve.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <ShapeFix_Edge.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Solid.hxx>
#include <StlAPI_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <cctype>
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

UnitsMethods_LengthUnit occUnit(StepUnit unit)
{
    switch (unit) {
    case StepUnit::Millimeter: return UnitsMethods_LengthUnit_Millimeter;
    case StepUnit::Inch: return UnitsMethods_LengthUnit_Inch;
    case StepUnit::Meter: return UnitsMethods_LengthUnit_Meter;
    }
    return UnitsMethods_LengthUnit_Millimeter;
}

// ---- Import helpers ----------------------------------------------------------

// Names come from an untrusted file and end up in the UI: printable, single
// spaces, at most 80 bytes (cut at a character boundary).
std::string cleanName(const std::string& raw)
{
    std::string out;
    for (char ch : raw) {
        const auto c = static_cast<unsigned char>(ch);
        const char kept = c < 0x20 || c == 0x7F ? ' ' : ch;
        if (kept == ' ' && (out.empty() || out.back() == ' '))
            continue;
        out.push_back(kept);
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    constexpr std::size_t kMaxBytes = 80;
    if (out.size() > kMaxBytes) {
        std::size_t cut = kMaxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut; // not inside a multi-byte character
        out.resize(cut);
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
    }
    return out;
}

// Placeholders that are not names: OCCT's writer calls unnamed products
// "Open CASCADE STEP translator 7.9 1"; other writers use "NONE" and the like.
bool meaningfulName(const std::string& name)
{
    if (name.empty() || name.rfind("Open CASCADE STEP translator", 0) == 0)
        return false;
    std::string lower;
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower != "none" && lower != "unknown" && lower != "noname" && lower != "unnamed" && lower != "$";
}

std::string labelName(const TDF_Label& label)
{
    Handle(TDataStd_Name) attribute;
    if (label.IsNull() || !label.FindAttribute(TDataStd_Name::GetID(), attribute))
        return {};
    const TCollection_AsciiString utf8(attribute->Get()); // converted to UTF-8
    return cleanName(utf8.ToCString());
}

struct Part {
    std::string name;
    TopoDS_Shape shape; // placed where the assembly puts it
};

// Flattens the assembly tree: every part instance with its accumulated
// placement. A part without a name of its own takes its assembly's.
void collectParts(const TDF_Label& label, const TopLoc_Location& location, const std::string& assemblyName,
                  std::vector<Part>& out, int depth)
{
    if (depth > 64 || out.size() > 100000)
        return; // absurdly deep or large (a malicious file): stop descending
    std::string name = labelName(label);
    if (!meaningfulName(name))
        name = assemblyName;
    if (XCAFDoc_ShapeTool::IsAssembly(label)) {
        TDF_LabelSequence components;
        XCAFDoc_ShapeTool::GetComponents(label, components);
        for (TDF_LabelSequence::Iterator it(components); it.More(); it.Next()) {
            TDF_Label referred;
            if (!XCAFDoc_ShapeTool::GetReferredShape(it.Value(), referred))
                continue;
            collectParts(referred, location * XCAFDoc_ShapeTool::GetLocation(it.Value()), name, out, depth + 1);
        }
        return;
    }
    const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(label);
    if (!shape.IsNull())
        out.push_back({name, location.IsIdentity() ? shape : shape.Moved(location)});
}

// Closes the XCAF document on every path out of the import.
struct XcafDocument {
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    XcafDocument() { app->NewDocument("MDTV-XCAF", doc); }
    ~XcafDocument()
    {
        try {
            if (!doc.IsNull() && doc->IsOpened())
                app->Close(doc);
        } catch (const Standard_Failure&) {
            // Nothing sensible to do while unwinding; the document is garbage now.
        }
    }
    XcafDocument(const XcafDocument&) = delete;
    XcafDocument& operator=(const XcafDocument&) = delete;
};

double signedVolume(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

// Curves on faces come from the file rounded (OCCT's own writer keeps 13
// digits). On cylinders, cones, spheres and tori they are rebuilt exactly
// from the 3D edges, which may put them in different periods: ShapeFix
// (usableSolid) joins them up again. With the file's curves, the first
// push/pull on an imported fillet left unorientable faces (2026-09-26).
// Changes the reader's fresh shape in place (nothing else holds it yet).
void rebuildCurvesOnRoundFaces(const TopoDS_Shape& shape)
{
    BRep_Builder builder;
    ShapeFix_Edge fixEdge;
    for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) {
        const TopoDS_Face& face = TopoDS::Face(f.Current());
        const GeomAbs_SurfaceType type = BRepAdaptor_Surface(face, Standard_False).GetType();
        if (type != GeomAbs_Cylinder && type != GeomAbs_Cone && type != GeomAbs_Sphere && type != GeomAbs_Torus)
            continue;
        for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(e.Current());
            if (BRep_Tool::Degenerated(edge))
                continue;
            const bool seam = BRep_Tool::IsClosed(edge, face);
            if (seam)
                builder.UpdateEdge(edge, Handle(Geom2d_Curve)(), Handle(Geom2d_Curve)(), face, BRep_Tool::Tolerance(edge));
            else
                builder.UpdateEdge(edge, Handle(Geom2d_Curve)(), face, BRep_Tool::Tolerance(edge));
            fixEdge.FixAddPCurve(edge, face, seam);
        }
    }
}

// A valid solid with a positive volume made from `solid` (repaired when
// needed, turned right side out when inside out), or a null shape.
TopoDS_Shape usableSolid(const TopoDS_Shape& solid)
{
    TopoDS_Shape s = solid;
    rebuildCurvesOnRoundFaces(s);
    if (!BRepCheck_Analyzer(s).IsValid()) {
        ShapeFix_Shape fix(s);
        fix.Perform();
        s = fix.Shape();
        if (s.IsNull())
            return {};
        if (s.ShapeType() != TopAbs_SOLID) {
            TopoDS_Shape only;
            int count = 0;
            for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next(), ++count)
                only = ex.Current();
            if (count != 1)
                return {};
            s = only;
        }
        if (!BRepCheck_Analyzer(s).IsValid())
            return {};
    }
    double v = signedVolume(s);
    if (v < 0) {
        s.Reverse();
        v = -v;
    }
    if (!(v > 1e-9))
        return {};
    return s;
}

struct Skipped {
    int openSurfaces = 0;
    int curves = 0;
    int damagedSolids = 0;
};

std::string plural(int n, const char* one, const char* many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// "2 open surfaces and 4 curves" (empty when nothing was skipped).
std::string describe(const Skipped& skipped)
{
    std::vector<std::string> parts;
    if (skipped.openSurfaces > 0)
        parts.push_back(plural(skipped.openSurfaces, "open surface", "open surfaces"));
    if (skipped.curves > 0)
        parts.push_back(plural(skipped.curves, "curve", "curves"));
    if (skipped.damagedSolids > 0)
        parts.push_back(plural(skipped.damagedSolids, "damaged solid", "damaged solids"));
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i)
        text += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return text;
}

// The closed solids of one part; what cannot become a solid is counted.
std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& shape, Skipped& skipped)
{
    std::vector<TopoDS_Shape> out;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
        const TopoDS_Shape solid = usableSolid(ex.Current());
        if (solid.IsNull())
            ++skipped.damagedSolids;
        else
            out.push_back(solid);
    }
    // Shells outside solids: a closed one is a solid written as surfaces.
    for (TopExp_Explorer ex(shape, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next()) {
        const TopoDS_Shell& shell = TopoDS::Shell(ex.Current());
        if (!BRep_Tool::IsClosed(shell)) {
            ++skipped.openSurfaces;
            continue;
        }
        ShapeFix_Solid fix;
        const TopoDS_Shape solid = usableSolid(fix.SolidFromShell(shell));
        if (solid.IsNull())
            ++skipped.openSurfaces;
        else
            out.push_back(solid);
    }
    for (TopExp_Explorer ex(shape, TopAbs_FACE, TopAbs_SHELL); ex.More(); ex.Next())
        ++skipped.openSurfaces; // loose faces
    for (TopExp_Explorer ex(shape, TopAbs_EDGE, TopAbs_FACE); ex.More(); ex.Next())
        ++skipped.curves;
    return out;
}

} // namespace

Status exportStep(const std::vector<NamedShape>& shapes, const std::filesystem::path& path, const StepWriteOptions& options)
{
    if (shapes.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to export.", "exportStep: no shapes");
    ScopedTimer timer("exportStep");
    try {
        std::lock_guard lock(stepMutex());
        // Through an XCAF document, so every product carries its body's name
        // exactly (STEPControl_Writer alone appends a running number to it):
        // other CAD programs show it as the part name, importStep names the
        // body after it.
        XcafDocument xcaf;
        if (xcaf.doc.IsNull())
            return Status::failure(ErrorCode::KernelFailure, "Unable to export STEP.", "exportStep: no XCAF document");
        const Handle(XCAFDoc_ShapeTool) tool = XCAFDoc_DocumentTool::ShapeTool(xcaf.doc->Main());
        BRep_Builder builder;
        int added = 0;
        for (const auto& s : shapes) {
            if (s.shape.isNull())
                continue;
            // Each body in a compound of its own: a new label even when two
            // bodies share one kernel shape (XCAF would merge them), and a
            // moved body keeps its place without becoming an assembly.
            TopoDS_Compound wrapper;
            builder.MakeCompound(wrapper);
            builder.Add(wrapper, occ(s.shape));
            const TDF_Label label = tool->AddShape(wrapper, Standard_False, Standard_False);
            TDataStd_Name::Set(label, TCollection_ExtendedString(s.name.c_str(), Standard_True /* UTF-8 */));
            ++added;
        }
        if (added == 0)
            return Status::failure(ErrorCode::InvalidArgument, "There is nothing to export.", "exportStep: only null shapes");
        // The writer first: constructing it registers the STEP parameters
        // (the first export of a run read none and wrote no curves on faces).
        STEPCAFControl_Writer writer;
        DESTEP_Parameters params;
        params.InitFromStatic();
        params.WriteSchema = DESTEP_Parameters::WriteMode_StepSchema_AP214IS;
        params.WriteUnit = occUnit(options.unit);
        params.WriteAssembly = DESTEP_Parameters::WriteMode_Assembly_Off; // one plain product per body
        writer.SetNameMode(Standard_True);
        writer.SetColorMode(Standard_False);
        writer.SetLayerMode(Standard_False);
        writer.SetPropsMode(Standard_False);
        writer.SetSHUOMode(Standard_False);
        writer.SetDimTolMode(Standard_False);
        writer.SetMaterialMode(Standard_False);
        const auto mode = options.surfacesOnly ? STEPControl_ShellBasedSurfaceModel : STEPControl_AsIs;
        if (!writer.Transfer(xcaf.doc, params, mode))
            return Status::failure(ErrorCode::FileWriteError, "Unable to export STEP.", "STEPCAFControl_Writer::Transfer failed");
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
    if (size == 0)
        return R::failure(ErrorCode::FileFormatError, "This STEP file is empty.", "importStep: empty file " + pathString(path));
    ScopedTimer timer("importStep");
    try {
        std::lock_guard lock(stepMutex());
        XcafDocument xcaf;
        if (xcaf.doc.IsNull())
            return R::failure(ErrorCode::KernelFailure, "The STEP file could not be read.", "importStep: no XCAF document");
        STEPCAFControl_Reader reader;
        reader.SetNameMode(true);
        reader.SetColorMode(false);
        reader.SetLayerMode(false);
        reader.SetPropsMode(false);
        reader.SetGDTMode(false);
        reader.SetSHUOMode(false);
        if (reader.ReadFile(pathString(path).c_str()) != IFSelect_RetDone)
            return R::failure(ErrorCode::FileFormatError, "This does not look like a valid STEP file.",
                              "STEPCAFControl_Reader::ReadFile failed: " + pathString(path));
        if (!reader.Transfer(xcaf.doc))
            return R::failure(ErrorCode::FileFormatError, "This STEP file contains no 3D shapes.",
                              "STEPCAFControl_Reader::Transfer transferred nothing: " + pathString(path));

        const Handle(XCAFDoc_ShapeTool) tool = XCAFDoc_DocumentTool::ShapeTool(xcaf.doc->Main());
        TDF_LabelSequence roots;
        tool->GetFreeShapes(roots);
        std::vector<Part> parts;
        for (TDF_LabelSequence::Iterator it(roots); it.More(); it.Next())
            collectParts(it.Value(), TopLoc_Location(), std::string(), parts, 0);

        std::vector<NamedShape> out;
        Skipped skipped;
        for (const Part& part : parts) {
            const std::vector<TopoDS_Shape> solids = solidsOf(part.shape, skipped);
            const bool named = meaningfulName(part.name);
            for (std::size_t k = 0; k < solids.size(); ++k) {
                std::string name;
                if (named)
                    name = solids.size() == 1 ? part.name : part.name + " " + std::to_string(k + 1);
                out.push_back({std::move(name), makeShape(solids[k])});
            }
            if (out.size() > kMaxImportSolids)
                return R::failure(ErrorCode::Unsupported,
                                  "This STEP file has more than " + std::to_string(kMaxImportSolids)
                                      + " solids, too many to import as bodies.",
                                  "importStep: too many solids in " + pathString(path));
        }
        const std::string what = describe(skipped);
        OS_LOG(Info, File) << "imported STEP " << pathString(path) << ": " << out.size() << " solids from " << parts.size()
                           << " parts" << (what.empty() ? std::string() : "; skipped " + what);
        if (out.empty())
            return R::failure(ErrorCode::FileFormatError,
                              what.empty() ? std::string("This STEP file contains no 3D shapes.")
                                           : "This STEP file has no closed solids to import (it has " + what
                                                 + "). Only solids can become bodies.",
                              "importStep: no usable solids in " + pathString(path));
        std::vector<std::string> warnings;
        if (!what.empty())
            warnings.push_back("Skipped " + what + ": only closed solids become bodies.");
        return R::success(std::move(out), std::move(warnings));
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
