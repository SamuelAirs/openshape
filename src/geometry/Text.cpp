// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Text.h"

#include "core/Timer.h"
#include "geometry/Modeling.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Font_FTFont.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_Buffer.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <StdPrs_BRepFont.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace os::geom {

using namespace detail;

namespace {

// ---- Registered fonts ----------------------------------------------------------------

struct FontFile {
    std::string id;
    std::string bytes;
    // Wraps `bytes` without owning them (a null allocator frees nothing):
    // FreeType reads the font from memory, never from a file.
    Handle(NCollection_Buffer) buffer;
    double capRatio = 0; // capital height / em size, measured on first use (under the registry lock)
};

std::mutex& registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::shared_ptr<FontFile>>& registry()
{
    static std::map<std::string, std::shared_ptr<FontFile>> fonts;
    return fonts;
}

std::shared_ptr<FontFile> findFont(const std::string& id)
{
    const std::lock_guard<std::mutex> lock(registryMutex());
    const auto it = registry().find(id);
    return it == registry().end() ? nullptr : it->second;
}

// OpenCASCADE's glyph-to-B-rep builder, fed from memory: its own Init takes
// a file path only. The size scale matches StdPrs_BRepFont::Init (72 pt at
// 4800 dpi: one em is `emSize` model units).
constexpr unsigned int kFontPoints = 72;
constexpr unsigned int kFontDpi = 4800;

class MemoryBRepFont : public StdPrs_BRepFont {
public:
    bool initFromMemory(const FontFile& file, double emSize)
    {
        myScaleUnits = emSize / double(kFontPoints) * 72.0 / double(kFontDpi);
        // Never borrow letters from system fonts: a step must give the same
        // letters on every machine.
        myFTFont->SetUseUnicodeSubsetFallback(Standard_False);
        return myFTFont->Init(file.buffer, TCollection_AsciiString(file.id.c_str()), Font_FTFontParams(kFontPoints, kFontDpi), 0);
    }
};

// ---- UTF-8 ------------------------------------------------------------------------------

// Strict UTF-8 (no overlong forms, no surrogates); nullopt when invalid.
std::optional<std::u32string> decodeUtf8(const std::string& text)
{
    std::u32string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        int extra = 0;
        char32_t c = 0;
        if (b0 < 0x80) {
            c = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            extra = 1;
            c = b0 & 0x1F;
        } else if ((b0 & 0xF0) == 0xE0) {
            extra = 2;
            c = b0 & 0x0F;
        } else if ((b0 & 0xF8) == 0xF0) {
            extra = 3;
            c = b0 & 0x07;
        } else {
            return std::nullopt;
        }
        if (i + std::size_t(extra) >= text.size())
            return std::nullopt; // cut off
        for (int k = 1; k <= extra; ++k) {
            const auto b = static_cast<unsigned char>(text[i + std::size_t(k)]);
            if ((b & 0xC0) != 0x80)
                return std::nullopt;
            c = (c << 6) | (b & 0x3F);
        }
        static constexpr char32_t kMinForLength[] = {0, 0x80, 0x800, 0x10000};
        if (c < kMinForLength[extra] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
            return std::nullopt;
        out.push_back(c);
        i += std::size_t(extra) + 1;
    }
    return out;
}

std::string encodeUtf8(char32_t c)
{
    std::string out;
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
    return out;
}

bool isSpace(char32_t c)
{
    return c == U' ' || c == 0xA0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000;
}

// ---- Glyphs -------------------------------------------------------------------------------

// The capital height per em (an "H"'s top), measured once per font.
double capRatio(FontFile& file)
{
    {
        const std::lock_guard<std::mutex> lock(registryMutex());
        if (file.capRatio > 0)
            return file.capRatio;
    }
    constexpr double em = 100.0;
    double ratio = 0.7; // a typical Latin font, should the font have no H
    Handle(MemoryBRepFont) font = new MemoryBRepFont();
    if (font->initFromMemory(file, em)) {
        const TopoDS_Shape h = font->RenderGlyph(U'H');
        if (!h.IsNull()) {
            Bnd_Box box;
            BRepBndLib::AddOptimal(h, box, Standard_False, Standard_False);
            if (!box.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                box.Get(x0, y0, z0, x1, y1, z1);
                if (y1 > 1.0)
                    ratio = y1 / em;
            }
        }
    }
    const std::lock_guard<std::mutex> lock(registryMutex());
    file.capRatio = ratio;
    return ratio;
}

struct Glyph {
    TopoDS_Shape shape; // at the pen position 0 (baseline y = 0)
    double pen = 0;     // x of its origin
};

// The positioned glyphs of `spec` at its size, plus their horizontal extent.
struct Layout {
    std::vector<Glyph> glyphs;
    double minX = 0, maxX = 0;
};

Result<Layout> layOut(const TextSpec& spec)
{
    using R = Result<Layout>;
    if (std::string why = checkText(spec); !why.empty())
        return R::failure(ErrorCode::InvalidArgument, why, "text: " + why);
    const std::shared_ptr<FontFile> file = findFont(spec.font);
    const std::u32string codepoints = *decodeUtf8(spec.text); // checked above
    Handle(MemoryBRepFont) font = new MemoryBRepFont();
    if (!font->initFromMemory(*file, spec.capHeight / capRatio(*file)))
        return R::failure(ErrorCode::KernelFailure, "Unable to read the font.", "text: font init failed for " + spec.font);
    Layout layout;
    bool any = false;
    double pen = 0;
    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        const char32_t c = codepoints[i];
        if (!isSpace(c)) {
            TopoDS_Shape shape = font->RenderGlyph(c);
            if (!shape.IsNull()) {
                Bnd_Box box;
                BRepBndLib::AddOptimal(shape, box, Standard_False, Standard_False);
                if (!box.IsVoid()) {
                    double x0, y0, z0, x1, y1, z1;
                    box.Get(x0, y0, z0, x1, y1, z1);
                    layout.minX = any ? std::min(layout.minX, pen + x0) : pen + x0;
                    layout.maxX = any ? std::max(layout.maxX, pen + x1) : pen + x1;
                    any = true;
                    layout.glyphs.push_back({shape, pen});
                }
            }
        }
        pen += font->AdvanceX(c, i + 1 < codepoints.size() ? codepoints[i + 1] : char32_t(0));
    }
    if (!any)
        return R::failure(ErrorCode::InvalidArgument, "Type some letters: spaces alone make nothing.", "text: no glyphs");
    return R::success(std::move(layout));
}

gp_Trsf frameTransform(const TextFrame& frame)
{
    const Vec3 n = frame.normal.normalized();
    Vec3 x = frame.xAxis - n * frame.xAxis.dot(n);
    if (x.length() < 1e-9)
        x = std::abs(n.z) < 0.9 ? Vec3{0, 0, 1}.cross(n) : Vec3{1, 0, 0};
    x = x.normalized();
    gp_Trsf trsf;
    trsf.SetTransformation(gp_Ax3(toPnt(frame.center), toDir(n), toDir(x)), gp_Ax3());
    return trsf;
}

double faceArea(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return std::abs(props.Mass());
}

// `face` (on the text's plane, z = 0) turned to face +Z if it faces -Z.
TopoDS_Face facingUp(TopoDS_Face face)
{
    BRepAdaptor_Surface surface(face);
    gp_Pnt p;
    gp_Vec du, dv;
    surface.D1((surface.FirstUParameter() + surface.LastUParameter()) / 2,
               (surface.FirstVParameter() + surface.LastVParameter()) / 2, p, du, dv);
    gp_Vec normal = du.Crossed(dv);
    if (face.Orientation() == TopAbs_REVERSED)
        normal.Reverse();
    if (normal.Z() < 0)
        face.Reverse();
    return face;
}

// Letters whose parts overlap become one face per piece of ink. A letter
// with a mark (an A's ring, a c's cedilla, an a's ogonek) is drawn from
// overlapping outlines in most fonts, Noto Sans included, and FreeType fills
// their union; as separate faces the overlap would count twice (in the
// letters' area, and as overlapping solids in the boolean). Faces whose
// boxes meet are fused (a planar boolean) and merged; a group that does not
// merge cleanly (the union's area must lie between the largest face's and
// the sum of them) keeps its faces, which the boolean still unites.
std::vector<TopoDS_Face> mergeOverlapping(std::vector<TopoDS_Face> faces)
{
    const std::size_t n = faces.size();
    if (n < 2)
        return faces;
    struct Box {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    std::vector<Box> boxes(n);
    for (std::size_t i = 0; i < n; ++i) {
        Bnd_Box box;
        BRepBndLib::AddOptimal(faces[i], box, Standard_False, Standard_False);
        double z0, z1;
        box.Get(boxes[i].x0, boxes[i].y0, z0, boxes[i].x1, boxes[i].y1, z1);
    }
    std::vector<std::size_t> parent(n);
    for (std::size_t i = 0; i < n; ++i)
        parent[i] = i;
    auto root = [&parent](std::size_t i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    // Sorted by left edge: a face can only meet those starting before it ends.
    std::vector<std::size_t> order(parent);
    std::sort(order.begin(), order.end(), [&boxes](std::size_t a, std::size_t b) { return boxes[a].x0 < boxes[b].x0; });
    constexpr double touch = 1e-6; // mm
    bool any = false;
    for (std::size_t a = 0; a < n; ++a)
        for (std::size_t b = a + 1; b < n; ++b) {
            const Box& p = boxes[order[a]];
            const Box& q = boxes[order[b]];
            if (q.x0 > p.x1 + touch)
                break;
            if (q.y0 <= p.y1 + touch && p.y0 <= q.y1 + touch) {
                parent[root(order[b])] = root(order[a]);
                any = true;
            }
        }
    if (!any)
        return faces;
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < n; ++i)
        groups[root(i)].push_back(i);
    std::vector<TopoDS_Face> out;
    for (const auto& [first, members] : groups) {
        if (members.size() == 1) {
            out.push_back(faces[members.front()]);
            continue;
        }
        double sum = 0, largest = 0;
        TopTools_ListOfShape arguments, tools;
        for (std::size_t i : members) {
            const double area = faceArea(faces[i]);
            sum += area;
            largest = std::max(largest, area);
            (i == members.front() ? arguments : tools).Append(faces[i]);
        }
        std::vector<TopoDS_Face> merged;
        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(arguments);
        fuse.SetTools(tools);
        fuse.SetNonDestructive(Standard_True);
        fuse.Build();
        if (fuse.IsDone() && !fuse.HasErrors()) {
            ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), Standard_False, Standard_True, Standard_False);
            unify.Build();
            double area = 0;
            for (TopExp_Explorer it(unify.Shape(), TopAbs_FACE); it.More(); it.Next()) {
                merged.push_back(facingUp(TopoDS::Face(it.Current())));
                area += faceArea(merged.back());
            }
            const double tolerance = 1e-7 * sum + 1e-9;
            bool valid = !merged.empty() && area >= largest - tolerance && area <= sum + tolerance;
            for (std::size_t k = 0; valid && k < merged.size(); ++k)
                valid = BRepCheck_Analyzer(merged[k]).IsValid();
            if (!valid) {
                OS_LOG(Debug, Geometry) << "text: overlapping letter parts not merged (area " << area << " of " << sum << ")";
                merged.clear();
            }
        }
        if (merged.empty())
            for (std::size_t i : members)
                merged.push_back(faces[i]);
        out.insert(out.end(), merged.begin(), merged.end());
    }
    return out;
}

// The letters as faces placed in `frame`, each face's wires oriented for an
// outward normal along frame.normal (FreeType outlines run the other way
// round; ShapeFix puts outer wires counter-clockwise and holes clockwise).
Result<Shape> buildFaces(const TextSpec& spec, const TextFrame& frame)
{
    using R = Result<Shape>;
    auto layout = layOut(spec);
    if (!layout)
        return R::failureFrom(layout);
    const double cx = (layout.value().minX + layout.value().maxX) / 2, cy = spec.capHeight / 2;
    // On the text's own plane first (z = 0, centered), then placed.
    std::vector<TopoDS_Face> letters;
    for (const Glyph& g : layout.value().glyphs) {
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(g.pen - cx, -cy, 0));
        // A copy per letter: repeated letters share one cached glyph.
        BRepBuilderAPI_Transform moved(g.shape, shift, Standard_True);
        for (TopExp_Explorer it(moved.Shape(), TopAbs_FACE); it.More(); it.Next()) {
            ShapeFix_Face fix(TopoDS::Face(it.Current()));
            fix.FixOrientation();
            TopoDS_Face face = fix.Face();
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            if (props.Mass() < 0)
                face.Reverse();
            letters.push_back(face);
        }
    }
    if (letters.empty())
        return R::failure(ErrorCode::InvalidArgument, "Type some letters: spaces alone make nothing.", "text: no faces");
    letters = mergeOverlapping(std::move(letters));
    BRep_Builder builder;
    TopoDS_Compound faces;
    builder.MakeCompound(faces);
    for (const TopoDS_Face& face : letters)
        builder.Add(faces, face);
    BRepBuilderAPI_Transform placed(faces, frameTransform(frame), Standard_True);
    return R::success(makeShape(placed.Shape()));
}

} // namespace

bool registerFont(const std::string& id, std::string bytes)
{
    if (id.empty() || bytes.empty())
        return false;
    auto file = std::make_shared<FontFile>();
    file->id = id;
    file->bytes = std::move(bytes);
    // Readable, and this OpenCASCADE renders glyphs (it may be built without FreeType).
    const auto usable = guarded("registerFont", "Unable to read the font.", [&]() -> Result<bool> {
        file->buffer = new NCollection_Buffer(Handle(NCollection_BaseAllocator)(), file->bytes.size(),
                                              reinterpret_cast<Standard_Byte*>(file->bytes.data()));
        Handle(MemoryBRepFont) font = new MemoryBRepFont();
        return Result<bool>::success(font->initFromMemory(*file, 10.0) && !font->RenderGlyph(U'H').IsNull());
    });
    if (!usable || !usable.value()) {
        OS_LOG(Warning, Geometry) << "font '" << id << "' cannot be used (not a font FreeType reads, or no font support)";
        return false;
    }
    const std::lock_guard<std::mutex> lock(registryMutex());
    registry()[id] = std::move(file);
    return true;
}

bool hasFont(const std::string& id)
{
    return findFont(id) != nullptr;
}

std::vector<std::string> registeredFonts()
{
    const std::lock_guard<std::mutex> lock(registryMutex());
    std::vector<std::string> ids;
    for (const auto& [id, file] : registry())
        ids.push_back(id);
    return ids;
}

std::string checkText(const TextSpec& spec)
{
    const std::shared_ptr<FontFile> file = findFont(spec.font);
    if (!file)
        return spec.font.empty() ? std::string("No font is available for text in this build.")
                                 : "The font \"" + spec.font + "\" is not available in this version of OpenShape.";
    static_assert(kMinCapHeight == 0.5 && kMaxCapHeight == 1000.0, "the message below names the limits");
    if (!(spec.capHeight >= kMinCapHeight) || !(spec.capHeight <= kMaxCapHeight))
        return "The size (the height of capital letters) must be between 0.5 and 1000 mm.";
    const auto codepoints = decodeUtf8(spec.text);
    if (!codepoints)
        return "The text contains characters that cannot be read.";
    bool letters = false;
    for (char32_t c : *codepoints)
        letters = letters || !isSpace(c);
    if (codepoints->empty() || !letters)
        return "Type the text first.";
    if (codepoints->size() > kMaxTextLength)
        return "The text is too long: at most " + std::to_string(kMaxTextLength) + " characters.";
    // The font only answers for what it has; ask it once. FreeType through
    // OpenCASCADE: the kernel lock (the preview worker may be making letters).
    const KernelLock kernel;
    Handle(Font_FTFont) font = new Font_FTFont();
    font->SetUseUnicodeSubsetFallback(Standard_False);
    if (!font->Init(file->buffer, TCollection_AsciiString(file->id.c_str()), Font_FTFontParams(kFontPoints, kFontDpi), 0))
        return "Unable to read the font.";
    for (char32_t c : *codepoints) {
        if (c == U'\n' || c == U'\r' || c == 0x2028 || c == 0x2029)
            return "Text is one line: type it without line breaks.";
        if (c < 0x20 || (c >= 0x7F && c < 0xA0))
            return "The text contains a control character.";
        if (!isSpace(c) && !font->HasSymbol(c))
            return "This font has no \"" + encodeUtf8(c) + "\": use another character.";
    }
    return {};
}

Result<Shape> textFaces(const TextSpec& spec)
{
    return placedTextFaces(spec, TextFrame{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
}

Result<Shape> placedTextFaces(const TextSpec& spec, const TextFrame& frame)
{
    if (frame.normal.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The text needs a flat face.", "text: zero normal");
    return guarded("textFaces", "Unable to make these letters.", [&] {
        ScopedTimer timer("textFaces");
        return buildFaces(spec, frame);
    });
}

Result<Shape> embossText(const Shape& body, const TextSpec& spec, const TextFrame& frame, double depth)
{
    using R = Result<Shape>;
    if (body.isNull())
        return R::failure(ErrorCode::InvalidArgument, "There is no body for the text.", "embossText: null shape");
    if (!std::isfinite(depth) || std::abs(depth) < 1e-3)
        return R::failure(ErrorCode::InvalidArgument, "Type a depth: positive raises the text, negative cuts it in.",
                          "embossText: depth " + std::to_string(depth));
    if (std::abs(depth) > 10000)
        return R::failure(ErrorCode::InvalidArgument, "The depth is too large.", "embossText: depth " + std::to_string(depth));
    if (frame.normal.length() < 1e-12)
        return R::failure(ErrorCode::InvalidArgument, "The text needs a flat face.", "embossText: zero normal");
    const bool raise = depth > 0;
    const char* userMessage = raise ? "Unable to raise this text here." : "Unable to cut this text here.";
    return guarded("embossText", userMessage, [&]() -> R {
        ScopedTimer timer("embossText");
        auto faces = buildFaces(spec, frame);
        if (!faces)
            return faces;
        // The letters start exactly on the face's plane (as a push/pull's
        // prism does): a lead into the material or above the surface would
        // add a sliver where the letters overhang a pocket, or nick a boss
        // standing on the face next to cut-in letters.
        const Vec3 n = frame.normal.normalized();
        BRepPrimAPI_MakePrism prism(occ(faces.value()), toVec(n * depth), Standard_True);
        prism.Build();
        if (!prism.IsDone())
            return R::failure(ErrorCode::KernelFailure, userMessage, "embossText: prism failed");
        TopTools_ListOfShape arguments, tools;
        arguments.Append(occ(body));
        double toolVolume = 0;
        for (TopExp_Explorer it(prism.Shape(), TopAbs_SOLID); it.More(); it.Next()) {
            TopoDS_Shape solid = it.Current();
            GProp_GProps props;
            BRepGProp::VolumeProperties(solid, props);
            if (props.Mass() < 0)
                solid.Reverse();
            toolVolume += std::abs(props.Mass());
            tools.Append(solid);
        }
        if (tools.IsEmpty())
            return R::failure(ErrorCode::KernelFailure, userMessage, "embossText: no letter solids");
        TopoDS_Shape out;
        if (raise) {
            BRepAlgoAPI_Fuse op;
            op.SetArguments(arguments);
            op.SetTools(tools);
            op.SetNonDestructive(Standard_True);
            op.Build();
            if (op.HasErrors() || !op.IsDone())
                return R::failure(ErrorCode::KernelFailure, userMessage, "embossText: fuse failed: " + describeAlgoErrors(op));
            out = op.Shape();
        } else {
            BRepAlgoAPI_Cut op;
            op.SetArguments(arguments);
            op.SetTools(tools);
            op.SetNonDestructive(Standard_True);
            op.Build();
            if (op.HasErrors() || !op.IsDone())
                return R::failure(ErrorCode::KernelFailure, userMessage, "embossText: cut failed: " + describeAlgoErrors(op));
            out = op.Shape();
        }
        ShapeUpgrade_UnifySameDomain unify(out, true, true, true);
        unify.Build();
        auto result = finishSolid(unify.Shape(), "embossText", userMessage);
        if (!result)
            return result;
        // What it must have done: change the volume the right way, by no more
        // than the letters hold (OCCT can report success with a wrong result).
        const double change = raise ? volume(result.value()) - volume(body) : volume(body) - volume(result.value());
        if (change <= 1e-9)
            return R::failure(ErrorCode::NoEffect,
                              raise ? "The raised text would add nothing here." : "The text does not reach into the part here.",
                              "embossText: volume change " + std::to_string(change));
        if (change > toolVolume * (1 + 1e-6) + 1e-6)
            return R::failure(ErrorCode::InvalidResultShape, userMessage,
                              "embossText: volume change " + std::to_string(change) + " > letters " + std::to_string(toolVolume));
        return result;
    });
}

} // namespace os::geom
