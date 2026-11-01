// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Several faces at once (0.3.1, the owner on his iPhone Duo, 2026-11-03: "I'd
// like to be able to select multiple faces and then drag the faces through an
// object to subtract"): a tap adds a face of the same body and Push/Pull stays
// (one arrow moves them all), a tap on a selected face takes it out, Clear and
// empty space start again, a long press takes every face of the step that made
// the face, and faces pushed through the face they stand on, or into another
// body, cut their shape out of it (engraved text), as one undo step.

#include "TestFonts.h"

#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "geometry/Text.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

using namespace os;
using namespace os::interact;

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    TypingPause::Clock::time_point now{};

    Harness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setClockForTesting([this] { return now; });
        controller.setViewportSize({1200, 800});
    }
    Uuid import(const std::string& name, const geom::Shape& shape)
    {
        EXPECT_TRUE(controller.importBodies({{name, shape}}, name + ".step").ok());
        for (const auto& b : document.bodies())
            if (b->name() == name)
                return b->id();
        return Uuid();
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    static PointerEvent at(Vec2 p, PointerDevice device)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        e.device = device;
        return e;
    }
    void tap(const Vec3& p, PointerDevice device = PointerDevice::Touch)
    {
        now += std::chrono::seconds(1); // apart: never a double-tap
        const PointerEvent e = at(screen(p), device);
        controller.pointerPress(e);
        now += std::chrono::milliseconds(60);
        controller.pointerRelease(e);
    }
    void longPress(const Vec3& p)
    {
        now += std::chrono::seconds(1);
        const PointerEvent e = at(screen(p), PointerDevice::Touch);
        controller.pointerPress(e);
        now += std::chrono::milliseconds(650);
        controller.pointerRelease(e);
    }
    double volume(const Uuid& body) const { return geom::volume(document.body(body)->shape()); }
    bool offers(const std::string& id) const
    {
        for (const auto& a : controller.contextActions())
            if (a.id == id)
                return true;
        return false;
    }
    const MultiPushPullOperation* multi() const { return dynamic_cast<const MultiPushPullOperation*>(controller.operation()); }
};

// A step: a 20 x 10 x 10 block beside a 20 x 10 x 5 one (tops at z = 10 and
// z = 5, both facing up), 3000 mm^3.
geom::Shape steppedBlock()
{
    const auto high = geom::makeBox({0, 0, 0}, {20, 10, 10}).value();
    const auto low = geom::makeBox({20, 0, 0}, {20, 10, 5}).value();
    return geom::booleanOp(high, low, geom::BooleanKind::Union).value();
}

// An STL file as triangles welded by their exact coordinates: closed (every
// edge used once each way) and enclosing a positive volume.
struct StlCheck {
    std::size_t triangles = 0;
    bool closed = false;
    double volume = 0;
};

StlCheck readStl(const std::filesystem::path& path)
{
    StlCheck out;
    std::ifstream in(path, std::ios::binary);
    char header[80];
    std::uint32_t count = 0;
    if (!in.read(header, 80) || !in.read(reinterpret_cast<char*>(&count), 4))
        return out;
    std::map<std::array<float, 3>, std::uint32_t> ids;
    std::vector<std::array<float, 3>> points;
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (std::uint32_t i = 0; i < count; ++i) {
        float data[12];
        std::uint16_t attribute = 0;
        if (!in.read(reinterpret_cast<char*>(data), sizeof data) || !in.read(reinterpret_cast<char*>(&attribute), 2))
            return out;
        std::uint32_t v[3];
        for (int k = 0; k < 3; ++k) {
            const std::array<float, 3> p{data[3 + 3 * k], data[4 + 3 * k], data[5 + 3 * k]};
            const auto [it, added] = ids.emplace(p, static_cast<std::uint32_t>(points.size()));
            if (added)
                points.push_back(p);
            v[k] = it->second;
        }
        for (int k = 0; k < 3; ++k)
            ++directed[{v[k], v[(k + 1) % 3]}];
        const auto& a = points[v[0]];
        const auto& b = points[v[1]];
        const auto& c = points[v[2]];
        out.volume += (double(a[0]) * (double(b[1]) * c[2] - double(b[2]) * c[1])
                       - double(a[1]) * (double(b[0]) * c[2] - double(b[2]) * c[0])
                       + double(a[2]) * (double(b[0]) * c[1] - double(b[1]) * c[0]))
                    / 6.0;
    }
    out.triangles = count;
    out.closed = count > 0;
    for (const auto& [edge, n] : directed)
        if (n != 1 || !directed.contains({edge.second, edge.first}))
            out.closed = false;
    return out;
}

StlCheck exportAndRead(const geom::Shape& shape, const char* name)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    EXPECT_TRUE(geom::exportStl({{name, shape}}, path).ok());
    const StlCheck check = readStl(path);
    std::filesystem::remove(path);
    return check;
}

} // namespace

// ---- Geometry ---------------------------------------------------------------

TEST(PushPullFaces, FacesFacingDifferentWaysAreRefused)
{
    const auto block = steppedBlock();
    std::vector<int> tops, sides;
    for (int i = 0; i < block.faceCount(); ++i)
        if (const auto f = geom::faceInfo(block, i); f && f->isPlanar() && f->normal.z > 0.999)
            tops.push_back(i);
        else if (f && f->isPlanar() && f->normal.x > 0.999)
            sides.push_back(i);
    ASSERT_EQ(tops.size(), 2u);
    ASSERT_FALSE(sides.empty());
    EXPECT_TRUE(geom::commonFaceNormal(block, tops).has_value());
    const std::vector<int> mixed{tops[0], sides[0]};
    EXPECT_FALSE(geom::commonFaceNormal(block, mixed).has_value());
    const auto refused = geom::pushPullFaces(block, mixed, 2);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error(), ErrorCode::InvalidArgument);
    // Both tops up 2 mm: 2 x 200 mm^2 x 2 more.
    const auto up = geom::pushPullFaces(block, tops, 2);
    ASSERT_TRUE(up) << up.developerMessage();
    EXPECT_NEAR(geom::volume(up.value()), 3000 + 800, 1e-6);
    // ...and down 3 mm: 2 x 200 x 3 less.
    const auto down = geom::pushPullFaces(block, tops, -3);
    ASSERT_TRUE(down) << down.developerMessage();
    EXPECT_NEAR(geom::volume(down.value()), 3000 - 1200, 1e-6);
}

// ---- Selecting several faces ----------------------------------------------------

TEST(MultiFace, ASecondFaceTappedKeepsPushPullAndOneArrowMovesBoth)
{
    Harness h;
    const Uuid block = h.import("Step", steppedBlock());
    h.controller.fitAll(false);
    h.tap({10, 5, 10}); // the high top
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Push/Pull");

    h.tap({30, 5, 5}); // the low top: added, and still Push/Pull
    ASSERT_EQ(h.controller.selection().size(), 2u);
    ASSERT_NE(h.multi(), nullptr) << "a second face does not switch to Shell";
    EXPECT_EQ(h.controller.operation()->title(), "Push/Pull");
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Distance");
    EXPECT_EQ(h.multi()->movingFaces().size(), 2u);
    EXPECT_EQ(h.controller.selectionSummary(), "2 faces \xC2\xB7 Gap 5.00 mm \xC2\xB7 parallel");
    EXPECT_TRUE(h.offers("clearSelection"));
    EXPECT_TRUE(h.offers("shell"));

    // Dragging either face drags the one arrow.
    const Vec2 face = h.screen({30, 5, 5});
    PointerEvent press = Harness::at(face, PointerDevice::Touch);
    h.controller.pointerPress(press);
    PointerEvent move = Harness::at(face + Vec2{0, -60}, PointerDevice::Touch);
    h.controller.pointerMove(move);
    EXPECT_TRUE(h.controller.manipulatorDragging() || h.controller.operation()->value() > 0)
        << "a finger on a selected face pulls them out";
    h.controller.pointerRelease(move);

    // Typed: both tops 3 mm higher, one step, one undo.
    EXPECT_EQ(h.controller.setValueText("3"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.volume(block), 3000 + 2 * 200 * 3, 1e-6);
    const auto& features = h.document.body(block)->features();
    EXPECT_EQ(features.back()->kind(), doc::FeatureKind::PushPullFaces);
    const auto rows = h.controller.historyRows();
    EXPECT_TRUE(std::any_of(rows.begin(), rows.end(), [](const HistoryRow& r) {
        return r.name == "Push/Pull" && r.detail == "3.00 mm \xC2\xB7 2 faces";
    }));
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(block), 3000, 1e-6);
}

TEST(MultiFace, TappingASelectedFaceTakesItOutAndEmptySpaceClears)
{
    Harness h;
    h.import("Step", steppedBlock());
    h.controller.fitAll(false);
    h.tap({10, 5, 10});
    h.tap({30, 5, 5});
    ASSERT_EQ(h.controller.selection().size(), 2u);
    h.tap({30, 5, 5}); // again: out
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.multi(), nullptr) << "one face: the single face's Push/Pull";
    EXPECT_EQ(h.controller.operation()->title(), "Push/Pull");
    // Empty space clears.
    h.now += std::chrono::seconds(1);
    const PointerEvent empty = Harness::at({5, 5}, PointerDevice::Touch);
    h.controller.pointerPress(empty);
    h.controller.pointerRelease(empty);
    EXPECT_TRUE(h.controller.selection().empty());
    // Clear does it in one tap.
    h.tap({10, 5, 10});
    h.tap({30, 5, 5});
    ASSERT_TRUE(h.controller.triggerAction("clearSelection").ok());
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.controller.operation(), nullptr);
}

TEST(MultiFace, FacesFacingDifferentWaysHaveNoArrowAndShellIsChosen)
{
    Harness h;
    h.import("Step", steppedBlock());
    h.controller.fitAll(false);
    h.tap({10, 5, 10}); // top
    h.tap({10, 0, 5}); // the high block's front, facing -Y
    ASSERT_EQ(h.controller.selection().size(), 2u);
    EXPECT_EQ(h.controller.operation(), nullptr) << "no arrow, and no Shell unless chosen";
    EXPECT_TRUE(h.offers("shell"));
    EXPECT_TRUE(h.offers("deleteFaces"));
    EXPECT_TRUE(h.offers("clearSelection"));
    EXPECT_FALSE(h.offers("pushpull"));
    // Shell chosen: the same selection model, each tap adds an opening.
    ASSERT_TRUE(h.controller.triggerAction("shell").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Shell");
    h.tap({30, 5, 5});
    EXPECT_EQ(h.controller.selection().size(), 3u);
    EXPECT_EQ(h.controller.operation()->title(), "Shell");
}

TEST(MultiFace, CmdClickAddsWithAMouseAPlainClickReplaces)
{
    Harness h;
    h.import("Step", steppedBlock());
    h.controller.fitAll(false);
    h.tap({10, 5, 10}, PointerDevice::Mouse);
    h.tap({30, 5, 5}, PointerDevice::Mouse);
    EXPECT_EQ(h.controller.selection().size(), 1u) << "a plain click replaces";
    // Cmd on an iPad keyboard arrives as Qt's Control modifier.
    h.now += std::chrono::seconds(1);
    PointerEvent cmd = Harness::at(h.screen({10, 5, 10}), PointerDevice::Mouse);
    cmd.modifiers.control = true;
    h.controller.pointerPress(cmd);
    h.controller.pointerRelease(cmd);
    EXPECT_EQ(h.controller.selection().size(), 2u);
    EXPECT_NE(h.multi(), nullptr);
}

// ---- Pushed into another body ------------------------------------------------------

TEST(MultiFace, TopsPushedThroughIntoTheBodyBelowCutItAsOneStep)
{
    Harness h;
    // A 40 x 20 x 5 plate, and on it a bar whose top a 2 mm slot splits in two
    // (two tops at z = 7, 14 x 10 mm each).
    const Uuid plate = h.import("Plate", geom::makeBox({0, 0, 0}, {40, 20, 5}).value());
    const auto bar = geom::makeBox({5, 5, 5}, {30, 10, 2}).value();
    const auto slot = geom::makeBox({19, 5, 6}, {2, 10, 1}).value();
    const Uuid slotted = h.import("Bar", geom::booleanOp(bar, slot, geom::BooleanKind::Subtract).value());
    ASSERT_NEAR(h.volume(slotted), 580, 1e-6);
    h.controller.setStandardView(StandardView::Top, false);
    h.controller.fitAll(false);
    h.tap({12, 10, 7});
    h.tap({28, 10, 7});
    ASSERT_EQ(h.controller.selection().size(), 2u);
    ASSERT_NE(h.multi(), nullptr);

    // 3 mm in: through the bar (2 mm) and 1 mm into the plate.
    EXPECT_EQ(h.controller.setValueText("-3"), "");
    ASSERT_TRUE(h.controller.operation()->hasPreview()) << h.controller.operation()->error();
    const auto& replaced = h.controller.operation()->previewMeshAlsoReplaces();
    EXPECT_NE(std::find(replaced.begin(), replaced.end(), plate), replaced.end()) << "the preview shows the plate cut";
    const std::size_t undoBefore = h.stack.size();
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_EQ(h.stack.size(), undoBefore + 1) << "one undo step";
    EXPECT_NEAR(h.volume(plate), 4000 - 2 * 140 * 1, 1e-6) << "the tops' shape, 1 mm deep";
    EXPECT_NEAR(h.volume(slotted), 20, 1e-6) << "only the slot's floor column is left of the bar";
    EXPECT_EQ(h.document.body(plate)->features().back()->kind(), doc::FeatureKind::SweepCut);

    // A clean STL of the cut plate.
    const StlCheck stl = exportAndRead(h.document.body(plate)->shape(), "openshape_sweepcut.stl");
    EXPECT_GT(stl.triangles, 12u);
    EXPECT_TRUE(stl.closed) << "every edge shared by two triangles, each way once";
    EXPECT_NEAR(stl.volume, 3720, 0.05);

    // The plate follows the push: a deeper push, set in the Model panel, cuts deeper.
    const Uuid push = h.document.body(slotted)->features().back()->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(push, "distance", "-4").ok());
    EXPECT_NEAR(h.volume(plate), 4000 - 2 * 140 * 2, 1e-6);

    // Undo: both as they were (the parameter, then the push and its cut).
    ASSERT_TRUE(h.controller.undo());
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(plate), 4000, 1e-6);
    EXPECT_NEAR(h.volume(slotted), 580, 1e-6);
    ASSERT_TRUE(h.controller.redo());
    EXPECT_NEAR(h.volume(plate), 3720, 1e-6);
}

TEST(MultiFace, PullingIntoAnotherBodyCutsNothing)
{
    Harness h;
    const Uuid plate = h.import("Plate", geom::makeBox({0, 0, 0}, {40, 20, 5}).value());
    const auto bar = geom::makeBox({5, 5, 5}, {30, 10, 2}).value();
    const auto slot = geom::makeBox({19, 5, 6}, {2, 10, 1}).value();
    h.import("Bar", geom::booleanOp(bar, slot, geom::BooleanKind::Subtract).value());
    h.controller.setStandardView(StandardView::Top, false);
    h.controller.fitAll(false);
    h.tap({12, 10, 7});
    h.tap({28, 10, 7});
    ASSERT_NE(h.multi(), nullptr);
    EXPECT_EQ(h.controller.setValueText("2"), "");
    EXPECT_TRUE(h.controller.operation()->previewMeshAlsoReplaces().empty());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.volume(plate), 4000, 1e-6);
    EXPECT_EQ(h.document.body(plate)->features().size(), 1u);
}

// ---- A long press: every face of the step ---------------------------------------------

TEST(MultiFace, LongPressOnRaisedTextSelectsItAndPushingItInEngravesTheFace)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    Harness h;
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {60, 30, 5};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(box));
    const Uuid plate = create->bodyId();
    ASSERT_TRUE(h.stack.push(std::move(create), h.document).ok());
    h.controller.documentChanged();
    h.controller.setStandardView(StandardView::Top, false);
    h.controller.fitAll(false);
    h.tap({40, 25, 5});
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_EQ(h.controller.setOperationText("Fold"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double letters = geom::surfaceArea(geom::textFaces({"Fold", doc::kTextFontRegular, 10}).value());
    ASSERT_NEAR(h.volume(plate), 9000 + letters, 1e-4 * letters);

    // A point on a letter's top (z = 6) that a mouse click picks as that face
    // (not an edge of the thin stroke beside it), zoomed in on the words.
    h.controller.wheel(h.screen({30, 15, 6}), 8);
    const geom::Shape& shape = h.document.body(plate)->shape();
    std::optional<Vec3> onLetter;
    int letterTops = 0;
    const auto mouse = InputProfile::forDevice(PointerDevice::Mouse);
    for (int i = 0; i < shape.faceCount(); ++i)
        if (const auto f = geom::faceInfo(shape, i); f && f->isPlanar() && f->normal.z > 0.999 && std::abs(f->point.z - 6) < 1e-6) {
            ++letterTops;
            for (const Vec3& candidate : {geom::pointOnFace(shape, i, f->centroid).value_or(f->point), f->point})
                if (const auto hit = h.controller.pickAt(h.screen(candidate), mouse);
                    !onLetter && hit.kind == sel::PickKind::Face && hit.index == i)
                    onLetter = candidate;
        }
    ASSERT_TRUE(onLetter.has_value());
    ASSERT_GE(letterTops, 4);

    h.messages.clear();
    h.longPress(*onLetter);
    const std::size_t selected = h.controller.selection().size();
    EXPECT_GT(selected, std::size_t(letterTops)) << "the tops and the walls";
    ASSERT_NE(h.multi(), nullptr);
    EXPECT_EQ(h.multi()->movingFaces().size(), std::size_t(letterTops)) << "the tops move; the walls come along";
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("faces of Text"), std::string::npos) << h.messages.back();

    // One letter's top taken out (Cmd-click: a finger near a thin stroke may
    // pick its edge), the others keep their arrow; then put back.
    auto cmdClick = [&] {
        h.now += std::chrono::seconds(1);
        PointerEvent e = Harness::at(h.screen(*onLetter), PointerDevice::Mouse);
        e.modifiers.control = true;
        h.controller.pointerPress(e);
        h.controller.pointerRelease(e);
    };
    cmdClick();
    EXPECT_EQ(h.controller.selection().size(), selected - 1);
    ASSERT_NE(h.multi(), nullptr) << "some of the step's faces: still pushed its way";
    EXPECT_EQ(h.multi()->movingFaces().size(), std::size_t(letterTops) - 1);
    cmdClick();
    EXPECT_EQ(h.controller.selection().size(), selected);
    ASSERT_NE(h.multi(), nullptr);
    EXPECT_EQ(h.multi()->movingFaces().size(), std::size_t(letterTops));

    // 1.5 mm in: the raised letters go, and 0.5 mm is engraved.
    EXPECT_EQ(h.controller.setValueText("-1.5"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.volume(plate), 9000 - 0.5 * letters, 1e-4 * letters);
    EXPECT_NEAR(geom::boundingBox(h.document.body(plate)->shape()).max.z, 5, 1e-6) << "nothing stands above the plate";
    const StlCheck stl = exportAndRead(h.document.body(plate)->shape(), "openshape_engraved.stl");
    EXPECT_TRUE(stl.closed);
    EXPECT_NEAR(stl.volume, 9000 - 0.5 * letters, 0.05 * letters);

    // A short tap is still one face.
    ASSERT_TRUE(h.controller.undo());
    h.controller.cancelOperation();
    h.controller.cancelOperation();
    h.tap(*onLetter);
    EXPECT_EQ(h.controller.selection().size(), 1u);
}
