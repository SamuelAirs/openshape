// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bodies and copies: duplicate, split into bodies, mirror/pattern as separate
// bodies, rotate about a picked edge or point.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <filesystem>

using namespace os;
using namespace os::test;
using namespace os::interact;

namespace {

struct Model {
    doc::Document document;
    cmd::UndoStack stack;

    Status push(std::unique_ptr<cmd::Command> command) { return stack.push(std::move(command), document); }

    // A w x h rectangle at the origin of the XY plane, its width dimensioned.
    Uuid rectangleSketch(double w, double h, sketch::EntityId* widthOut = nullptr)
    {
        sketch::Sketch s(Uuid::generate(), sketch::Plane::xy());
        s.setName(document.nextSketchName());
        const auto r = sketch::addRectangle(s, {0, 0}, {w, h}, sketch::kOriginId);
        const auto width = s.addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], w});
        s.addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], h});
        EXPECT_TRUE(sketch::solve(s).ok);
        if (widthOut)
            *widthOut = width;
        const Uuid id = s.id();
        EXPECT_TRUE(push(std::make_unique<cmd::CreateSketchCommand>(std::move(s))).ok());
        return id;
    }

    void setSketchDimension(const Uuid& sketchId, sketch::EntityId constraint, double value)
    {
        sketch::Sketch copy = *document.sketch(sketchId);
        copy.constraint(constraint)->value = value;
        EXPECT_TRUE(sketch::solve(copy).ok);
        EXPECT_TRUE(push(std::make_unique<cmd::EditSketchCommand>(copy, "Edit sketch")).ok());
    }

    Uuid extrude(const Uuid& sketchId, double distance)
    {
        const sketch::Sketch& s = *document.sketch(sketchId);
        auto regions = doc::sketchRegions(s);
        EXPECT_TRUE(regions.ok() && !regions.value().empty());
        auto f = std::make_unique<doc::ExtrudeFeature>();
        f->sketchId = sketchId;
        f->profiles = {doc::makeProfileRef(regions.value().front(), s)};
        f->distance = distance;
        auto command = std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), std::move(f));
        const Uuid id = command->bodyId();
        EXPECT_TRUE(push(std::move(command)).ok());
        return id;
    }

    Uuid box(const std::string& name, Vec3 origin, Vec3 size)
    {
        auto f = std::make_unique<doc::BoxFeature>();
        f->origin = origin;
        f->size = size;
        auto command = std::make_unique<cmd::CreateBodyCommand>(name, std::move(f));
        const Uuid id = command->bodyId();
        EXPECT_TRUE(push(std::move(command)).ok());
        return id;
    }

    const doc::Body& body(const Uuid& id) const { return *document.body(id); }
    double volume(const Uuid& id) const { return geom::volume(body(id).shape()); }
    geom::BoundingBox bounds(const Uuid& id) const { return geom::boundingBox(body(id).shape()); }
};

// A 60 x 20 x 5 plate cut in two by a slot across it: the slot's left edge at
// x = 28, its width dimensioned (4 mm: two 28 mm pieces).
struct SlottedPlate {
    Uuid plate;
    Uuid slotSketch;
    sketch::EntityId slotWidth = 0;
};

SlottedPlate slottedPlate(Model& m)
{
    SlottedPlate out;
    out.plate = m.box("Body 1", {0, 0, 0}, {60, 20, 5});
    sketch::Sketch s(Uuid::generate(), sketch::Plane::xy());
    s.setName(m.document.nextSketchName());
    const auto r = sketch::addRectangle(s, {28, -5}, {32, 25});
    s.addConstraint({sketch::ConstraintKind::HorizontalDistance, sketch::kOriginId, r.corners[0], 28});
    s.addConstraint({sketch::ConstraintKind::VerticalDistance, sketch::kOriginId, r.corners[0], -5});
    out.slotWidth = s.addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 4});
    s.addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 30});
    EXPECT_TRUE(sketch::solve(s).ok);
    out.slotSketch = s.id();
    EXPECT_TRUE(m.push(std::make_unique<cmd::CreateSketchCommand>(std::move(s))).ok());

    const sketch::Sketch& sk = *m.document.sketch(out.slotSketch);
    auto regions = doc::sketchRegions(sk);
    EXPECT_TRUE(regions.ok() && regions.value().size() == 1);
    auto cut = std::make_unique<doc::ExtrudeFeature>();
    cut->sketchId = out.slotSketch;
    cut->profiles = {doc::makeProfileRef(regions.value().front(), sk)};
    cut->distance = 1;
    cut->mode = doc::ExtrudeMode::Cut;
    cut->throughAll = true;
    cut->symmetric = true;
    EXPECT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(out.plate, std::move(cut))).ok());
    return out;
}

std::filesystem::path tempPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_bodies_" + name);
}

void expectBox(const geom::BoundingBox& bb, Vec3 min, Vec3 max)
{
    EXPECT_NEAR(bb.min.x, min.x, 1e-6);
    EXPECT_NEAR(bb.min.y, min.y, 1e-6);
    EXPECT_NEAR(bb.min.z, min.z, 1e-6);
    EXPECT_NEAR(bb.max.x, max.x, 1e-6);
    EXPECT_NEAR(bb.max.y, max.y, 1e-6);
    EXPECT_NEAR(bb.max.z, max.z, 1e-6);
}

// Split into bodies as builds before independent pieces did it (what older
// files hold): a Split step, plus a body per other piece whose base SplitPiece
// step takes that piece from this body.
std::unique_ptr<cmd::Command> legacySplitCommand(const doc::Document& document, const Uuid& bodyId)
{
    std::vector<geom::SolidSignature> pieces;
    for (const geom::Shape& s : geom::solids(document.body(bodyId)->shape()))
        pieces.push_back(geom::solidSignature(s));
    std::sort(pieces.begin(), pieces.end(), [](const geom::SolidSignature& a, const geom::SolidSignature& b) {
        if (std::abs(a.volume - b.volume) > 1e-6 * std::max(a.volume, b.volume))
            return a.volume > b.volume;
        if (std::abs(a.centroid.x - b.centroid.x) > 1e-9)
            return a.centroid.x < b.centroid.x;
        if (std::abs(a.centroid.y - b.centroid.y) > 1e-9)
            return a.centroid.y < b.centroid.y;
        return a.centroid.z < b.centroid.z;
    });
    auto split = std::make_unique<doc::SplitFeature>();
    split->pieces = pieces;
    const Uuid splitId = split->id();
    std::vector<std::unique_ptr<cmd::Command>> steps;
    steps.push_back(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(split)));
    const std::vector<std::string> names = document.nextBodyNames(pieces.size() - 1);
    for (std::size_t k = 1; k < pieces.size(); ++k) {
        auto piece = std::make_unique<doc::SplitPieceFeature>();
        piece->sourceBody = bodyId;
        piece->splitFeature = splitId;
        piece->piece = static_cast<int>(k);
        steps.push_back(std::make_unique<cmd::CreateBodyCommand>(names[k - 1], std::move(piece)));
    }
    return std::make_unique<cmd::CompositeCommand>("Split into bodies", std::move(steps));
}

// Headless UI: the controller fed like the Qt layer feeds it.
struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;

    Harness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
    }

    Uuid addBox(const std::string& name, Vec3 origin, Vec3 size)
    {
        auto f = std::make_unique<doc::BoxFeature>();
        f->origin = origin;
        f->size = size;
        EXPECT_TRUE(stack.push(std::make_unique<cmd::CreateBodyCommand>(name, std::move(f)), document).ok());
        controller.documentChanged();
        return document.bodies().back()->id();
    }

    static PointerEvent at(Vec2 p, PointerDevice device = PointerDevice::Mouse)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        e.device = device;
        return e;
    }
    void clickAt(Vec2 p, PointerDevice device = PointerDevice::Mouse)
    {
        controller.pointerPress(at(p, device));
        controller.pointerRelease(at(p, device));
    }
    bool saw(const std::string& text) const
    {
        for (const auto& m : messages)
            if (m.find(text) != std::string::npos)
                return true;
        return false;
    }
    const HistoryRow* row(const Uuid& id) const
    {
        rows = controller.historyRows();
        for (const auto& r : rows)
            if (r.id == id)
                return &r;
        return nullptr;
    }
    mutable std::vector<HistoryRow> rows;
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }

    bool hasAction(const std::string& id) const
    {
        for (const auto& a : controller.contextActions())
            if (a.id == id)
                return true;
        return false;
    }
};

} // namespace

// ---- Duplicate ---------------------------------------------------------------------

// The copy has its own history and its own sketch: editing either side never
// changes the other.
TEST(Duplicate, CopyIsIndependentOfTheSource)
{
    Model m;
    sketch::EntityId width = 0;
    const Uuid sketchId = m.rectangleSketch(30, 20, &width);
    const Uuid source = m.extrude(sketchId, 10);
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(source, filletVertical(m.body(source).shape(), 2.0))).ok());
    const double volume = m.volume(source);
    EXPECT_NEAR(volume, 6000.0 - 4 * (4 - kPi) * 10, 1e-3);

    auto command = std::make_unique<cmd::DuplicateBodyCommand>(source);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());
    EXPECT_EQ(m.stack.undoLabel(), "Duplicate");
    ASSERT_EQ(m.document.bodies().size(), 2u);
    ASSERT_NE(m.document.body(copy), nullptr);
    EXPECT_EQ(m.body(copy).name(), "Body 1 copy");
    EXPECT_TRUE(m.body(copy).isVisible());
    EXPECT_NEAR(m.volume(copy), volume, 1e-6);
    EXPECT_FALSE(m.body(copy).hasFailures());

    // Fresh ids everywhere; the copy's extrusion uses a copy of the sketch.
    const auto& copied = m.body(copy).features();
    ASSERT_EQ(copied.size(), 2u);
    for (std::size_t i = 0; i < copied.size(); ++i)
        EXPECT_NE(copied[i]->id(), m.body(source).features()[i]->id());
    const auto& copyExtrude = static_cast<const doc::ExtrudeFeature&>(*copied[0]);
    ASSERT_EQ(m.document.sketches().size(), 2u);
    EXPECT_NE(copyExtrude.sketchId, sketchId);
    const sketch::Sketch* copySketch = m.document.sketch(copyExtrude.sketchId);
    ASSERT_NE(copySketch, nullptr);
    EXPECT_EQ(copySketch->name(), "Sketch 1 copy");
    EXPECT_FALSE(copySketch->isVisible()) << "it would sit exactly on the source's sketch";

    // Editing a step of the copy leaves the source alone.
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(copyExtrude.id(), "distance", 15.0)).ok());
    EXPECT_NEAR(m.bounds(copy).size().z, 15.0, 1e-6);
    EXPECT_NEAR(m.bounds(source).size().z, 10.0, 1e-6);
    // Editing the source's sketch leaves the copy alone...
    m.setSketchDimension(sketchId, width, 40);
    EXPECT_NEAR(m.bounds(source).size().x, 40.0, 1e-6);
    EXPECT_NEAR(m.bounds(copy).size().x, 30.0, 1e-6);
    // ...and the other way round.
    m.setSketchDimension(copyExtrude.sketchId, width, 50);
    EXPECT_NEAR(m.bounds(copy).size().x, 50.0, 1e-6);
    EXPECT_NEAR(m.bounds(source).size().x, 40.0, 1e-6);

    // One undo step each; undoing the duplicate removes the copy and its sketch.
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_NEAR(m.volume(copy), volume, 1e-6);
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_EQ(m.document.bodies().size(), 1u);
    EXPECT_EQ(m.document.sketches().size(), 1u);
    ASSERT_TRUE(m.stack.redo(m.document).ok());
    ASSERT_NE(m.document.body(copy), nullptr) << "redo recreates the same identity";
    EXPECT_NEAR(m.volume(copy), volume, 1e-6);
    EXPECT_EQ(m.document.sketches().size(), 2u);
}

// A body that consumed another (Subtract) takes a hidden copy of that tool
// along: editing the source's tool does not change the copy.
TEST(Duplicate, ConsumedToolBodyIsCopiedToo)
{
    Model m;
    const Uuid a = m.box("Body 1", {0, 0, 0}, {20, 20, 20});
    const Uuid b = m.box("Body 2", {5, 5, 15}, {10, 10, 10});
    auto combine = std::make_unique<doc::CombineFeature>();
    combine->toolBody = b;
    combine->mode = doc::CombineMode::Subtract;
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(a, std::move(combine))).ok());
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetBodyVisibilityCommand>(b, false)).ok());
    EXPECT_NEAR(m.volume(a), 8000.0 - 500.0, 1e-6);

    auto command = std::make_unique<cmd::DuplicateBodyCommand>(a);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());
    ASSERT_EQ(m.document.bodies().size(), 4u);
    EXPECT_NEAR(m.volume(copy), 7500.0, 1e-6);
    const auto& copyCombine = static_cast<const doc::CombineFeature&>(*m.body(copy).features()[1]);
    ASSERT_NE(copyCombine.toolBody, b);
    const doc::Body* toolCopy = m.document.body(copyCombine.toolBody);
    ASSERT_NE(toolCopy, nullptr);
    EXPECT_EQ(toolCopy->name(), "Body 2 copy");
    EXPECT_FALSE(toolCopy->isVisible());

    // A lower tool cuts less from the source; the copy keeps its own tool.
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(b).features()[0]->id(), "height", 3.0)).ok());
    EXPECT_NEAR(m.volume(a), 8000.0 - 300.0, 1e-6);
    EXPECT_NEAR(m.volume(copy), 7500.0, 1e-6);

    ASSERT_TRUE(m.stack.undo(m.document));
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_EQ(m.document.bodies().size(), 2u);
}

// A second duplicate of the same body gets the next free name.
TEST(Duplicate, NamesStayUnique)
{
    Model m;
    const Uuid a = m.box("Body 1", {0, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(m.push(std::make_unique<cmd::DuplicateBodyCommand>(a)).ok());
    ASSERT_TRUE(m.push(std::make_unique<cmd::DuplicateBodyCommand>(a)).ok());
    ASSERT_EQ(m.document.bodies().size(), 3u);
    EXPECT_EQ(m.document.bodies()[1]->name(), "Body 1 copy");
    EXPECT_EQ(m.document.bodies()[2]->name(), "Body 1 copy 2");
    EXPECT_FALSE(m.push(std::make_unique<cmd::DuplicateBodyCommand>(Uuid::generate())).ok());
    EXPECT_EQ(m.document.bodies().size(), 3u);
}

// The copy takes over the source's step results instead of computing its
// history again (the same shapes, shared); computing it anew gives the same
// body, and editing the copy computes only the copy.
TEST(Duplicate, CopyTakesOverTheSourceResults)
{
    Model m;
    const Uuid a = m.box("Body 1", {0, 0, 0}, {30, 20, 10});
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(a, filletVertical(m.body(a).shape(), 2.0))).ok());
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(a, pushPull(m.body(a).shape(), {0, 0, 1}, 5))).ok());
    auto command = std::make_unique<cmd::DuplicateBodyCommand>(a);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(m.body(copy).state(i).status, doc::FeatureStatus::Ok) << i;
        EXPECT_TRUE(m.body(copy).state(i).output.sameAs(m.body(a).state(i).output)) << i;
    }
    EXPECT_NE(m.body(copy).shapeRevision(), m.body(a).shapeRevision()) << "its own mesh";
    const double volume = m.volume(copy);
    EXPECT_NEAR(volume, (30 * 20 - 4 * (4 - kPi)) * 15.0, 1e-3);
    const int faces = m.body(copy).shape().faceCount();
    m.document.recomputeAll();
    EXPECT_NEAR(m.volume(copy), volume, 1e-6);
    EXPECT_EQ(m.body(copy).shape().faceCount(), faces);
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(copy).features()[1]->id(), "size", 4.0)).ok());
    EXPECT_NEAR(m.volume(copy), (30 * 20 - 16 * (4 - kPi)) * 15.0, 1e-3);
    EXPECT_NEAR(m.volume(a), volume, 1e-6);
}

TEST(Duplicate, SaveAndReopen)
{
    Model m;
    sketch::EntityId width = 0;
    const Uuid sketchId = m.rectangleSketch(30, 20, &width);
    const Uuid source = m.extrude(sketchId, 10);
    auto command = std::make_unique<cmd::DuplicateBodyCommand>(source);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());

    const auto path = tempPath("duplicate.openshape");
    ASSERT_TRUE(io::saveProject(m.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_EQ(d.bodies().size(), 2u);
    ASSERT_NE(d.body(copy), nullptr);
    EXPECT_NEAR(geom::volume(d.body(copy)->shape()), 6000.0, 1e-6);
    const Uuid copySketch = static_cast<const doc::ExtrudeFeature&>(*d.body(copy)->features()[0]).sketchId;
    ASSERT_NE(d.sketch(copySketch), nullptr);
    EXPECT_NE(copySketch, sketchId);

    // Still independent after reopening.
    sketch::Sketch edited = *d.sketch(copySketch);
    edited.constraint(width)->value = 45;
    ASSERT_TRUE(sketch::solve(edited).ok);
    d.replaceSketch(edited);
    EXPECT_NEAR(geom::boundingBox(d.body(copy)->shape()).size().x, 45.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(d.body(source)->shape()).size().x, 30.0, 1e-6);
    std::filesystem::remove(path);
}

// The UI flow: Duplicate selects the copy with the Move arrows armed, so it
// can be dragged (or typed) away; each step undoes on its own.
TEST(Duplicate, CopyIsSelectedWithMoveArrows)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0)..(10,10,20)
    h.controller.fitAll(false);
    const Uuid source = h.document.bodies().front()->id();
    EXPECT_FALSE(h.controller.triggerAction("duplicate").ok()) << "nothing selected";
    ASSERT_TRUE(h.controller.selectBody(source, false).ok());
    EXPECT_TRUE(h.hasAction("duplicate"));
    ASSERT_TRUE(h.controller.triggerAction("duplicate").ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    const Uuid copy = h.document.bodies().back()->id();
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Body);
    EXPECT_EQ(h.controller.selection().items()[0].bodyId, copy);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Move");
    EXPECT_EQ(h.controller.operation()->bodyId(), copy);

    EXPECT_EQ(h.controller.setValueText("30"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::boundingBox(h.document.body(copy)->shape()).min.x, 20.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(h.document.body(source)->shape()).min.x, -10.0, 1e-6);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(geom::boundingBox(h.document.body(copy)->shape()).min.x, -10.0, 1e-6);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);

    // A selected face duplicates its body too.
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_TRUE(h.controller.triggerAction("duplicate").ok());
    EXPECT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(geom::volume(h.document.bodies().back()->shape()), 8000.0, 1e-6);
}

// ---- Split into bodies -------------------------------------------------------------

// A plate cut in two by a slot becomes two bodies. Each piece is independent:
// a copy of the plate's history (with its own hidden copy of the slot sketch)
// ending in a Split step that keeps that piece, so editing one never changes
// the other. Undo, redo and a saved file keep it.
TEST(Split, PlateCutInTwoBecomesTwoIndependentBodies)
{
    Model m;
    const SlottedPlate p = slottedPlate(m);
    ASSERT_EQ(m.body(p.plate).shape().solidCount(), 2);
    EXPECT_NEAR(m.volume(p.plate), 2 * 2800.0, 1e-6);

    int pieces = 0;
    auto split = cmd::makeSplitBodyCommand(m.document, p.plate, &pieces);
    ASSERT_TRUE(split.ok()) << split.developerMessage();
    EXPECT_EQ(pieces, 2);
    ASSERT_TRUE(m.push(std::move(split.value())).ok());
    EXPECT_EQ(m.stack.undoLabel(), "Split into bodies");
    ASSERT_EQ(m.document.bodies().size(), 2u);
    const Uuid piece = m.document.bodies()[1]->id();
    EXPECT_EQ(m.body(piece).name(), "Body 2");
    EXPECT_EQ(m.body(p.plate).shape().solidCount(), 1);
    EXPECT_EQ(m.body(piece).shape().solidCount(), 1);
    // Equal pieces: the body keeps the one nearer the origin.
    EXPECT_NEAR(m.volume(p.plate), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    EXPECT_NEAR(m.bounds(p.plate).max.x, 28.0, 1e-6);
    EXPECT_NEAR(m.bounds(piece).min.x, 32.0, 1e-6);
    // The piece's history: the plate's (box, cut), then its own Split step.
    ASSERT_EQ(m.body(piece).features().size(), 3u);
    EXPECT_EQ(m.body(piece).features()[0]->kind(), doc::FeatureKind::Box);
    EXPECT_EQ(m.body(piece).features()[2]->kind(), doc::FeatureKind::Split);
    EXPECT_EQ(m.body(p.plate).features().back()->kind(), doc::FeatureKind::Split);
    const Uuid pieceBox = m.body(piece).features()[0]->id();
    const Uuid pieceSketch = static_cast<const doc::ExtrudeFeature&>(*m.body(piece).features()[1]).sketchId;
    ASSERT_NE(pieceSketch, p.slotSketch) << "its own copy of the slot sketch";
    ASSERT_NE(m.document.sketch(pieceSketch), nullptr);
    EXPECT_FALSE(m.document.sketch(pieceSketch)->isVisible());
    EXPECT_TRUE(m.document.bodiesUsing(p.plate).empty()) << "nothing is built from the plate";

    // An 80 mm plate: the plate keeps its left piece; the piece does not grow.
    const Uuid plateBox = m.body(p.plate).features()[0]->id();
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(plateBox, "width", 80.0)).ok());
    EXPECT_FALSE(m.body(p.plate).hasFailures());
    EXPECT_NEAR(m.volume(p.plate), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    EXPECT_NEAR(m.bounds(piece).max.x, 60.0, 1e-6);
    // A wider slot in the plate's sketch: the piece keeps its own.
    m.setSketchDimension(p.slotSketch, p.slotWidth, 10);
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    EXPECT_NEAR(m.bounds(piece).min.x, 32.0, 1e-6);
    // And the other way round: a taller piece leaves the plate alone.
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(pieceBox, "height", 8.0)).ok());
    EXPECT_FALSE(m.body(piece).hasFailures());
    EXPECT_NEAR(m.volume(piece), 28 * 20 * 8.0, 1e-6);
    EXPECT_NEAR(m.volume(p.plate), 2800.0, 1e-6);
    EXPECT_NEAR(m.bounds(p.plate).size().z, 5.0, 1e-6);

    // Undo the three edits, then the split (one step); redo it.
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_EQ(m.document.bodies().size(), 1u);
    EXPECT_EQ(m.document.sketches().size(), 1u);
    EXPECT_EQ(m.body(p.plate).shape().solidCount(), 2);
    ASSERT_TRUE(m.stack.redo(m.document).ok());
    ASSERT_EQ(m.document.bodies().size(), 2u);
    ASSERT_NE(m.document.body(piece), nullptr) << "redo recreates the same identity";
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(p.plate), 2800.0, 1e-6);

    // Save and reopen: still two independent bodies.
    const auto path = tempPath("split.openshape");
    ASSERT_TRUE(io::saveProject(m.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_EQ(d.bodies().size(), 2u);
    EXPECT_NEAR(geom::volume(d.bodies()[0]->shape()), 2800.0, 1e-6);
    EXPECT_NEAR(geom::volume(d.bodies()[1]->shape()), 2800.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(d.bodies()[1]->shape()).min.x, 32.0, 1e-6);
    EXPECT_FALSE(d.bodies()[1]->hasFailures());
    std::filesystem::remove(path);

    // The plate can be deleted (nothing is built from it); the piece stays.
    ASSERT_TRUE(m.push(std::make_unique<cmd::DeleteBodyCommand>(p.plate)).ok());
    EXPECT_FALSE(m.body(piece).hasFailures());
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);

    // A body in one piece cannot be split.
    const Uuid single = m.box("Body 9", {100, 0, 0}, {5, 5, 5});
    EXPECT_FALSE(cmd::makeSplitBodyCommand(m.document, single).ok());
}

// Three pieces: the body keeps the largest; two new bodies, each with hidden
// copies of the two tool bodies it was cut by. Widening a cut of the body
// changes only the body. A new cut before the body's Split step adds a piece,
// which stays in the body rather than vanishing.
TEST(Split, ThreePiecesAreIndependentBodies)
{
    Model m;
    const Uuid bar = m.box("Body 1", {0, 0, 0}, {100, 10, 10});
    const Uuid cutA = m.box("Cut A", {20, -1, -1}, {2, 12, 12});
    const Uuid cutB = m.box("Cut B", {70, -1, -1}, {2, 12, 12});
    for (const Uuid& tool : {cutA, cutB}) {
        auto combine = std::make_unique<doc::CombineFeature>();
        combine->toolBody = tool;
        combine->mode = doc::CombineMode::Subtract;
        ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(bar, std::move(combine))).ok());
        ASSERT_TRUE(m.push(std::make_unique<cmd::SetBodyVisibilityCommand>(tool, false)).ok());
    }
    ASSERT_EQ(m.body(bar).shape().solidCount(), 3); // 20, 48 and 28 long
    auto split = cmd::makeSplitBodyCommand(m.document, bar);
    ASSERT_TRUE(split.ok());
    ASSERT_TRUE(m.push(std::move(split.value())).ok());
    // The bar and its tools, then per piece: its copies of the tools, the piece.
    ASSERT_EQ(m.document.bodies().size(), 9u);
    const Uuid piece2 = m.document.bodies()[5]->id();
    const Uuid piece3 = m.document.bodies()[8]->id();
    EXPECT_EQ(m.body(piece2).name(), "Body 2");
    EXPECT_EQ(m.body(piece3).name(), "Body 3");
    EXPECT_EQ(m.document.bodies()[3]->name(), "Cut A copy");
    EXPECT_EQ(m.document.bodies()[7]->name(), "Cut B copy 2");
    for (std::size_t i : {1u, 2u, 3u, 4u, 6u, 7u})
        EXPECT_FALSE(m.document.bodies()[i]->isVisible()) << i;
    EXPECT_NEAR(m.volume(bar), 4800.0, 1e-6) << "keeps the largest piece";
    EXPECT_NEAR(m.volume(piece2), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(piece3), 2000.0, 1e-6);
    EXPECT_TRUE(m.document.bodiesUsing(bar).empty());

    // A wider cut A (the bar's own tool): only the bar changes; so does a
    // shorter piece 3 (its own box) leave the bar alone.
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(cutA).features()[0]->id(), "width", 4.0)).ok());
    EXPECT_NEAR(m.volume(bar), 4600.0, 1e-6);
    EXPECT_NEAR(m.volume(piece2), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(piece3), 2000.0, 1e-6);
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(piece3).features()[0]->id(), "height", 5.0)).ok());
    EXPECT_NEAR(m.volume(piece3), 1000.0, 1e-6);
    EXPECT_NEAR(m.volume(bar), 4600.0, 1e-6);

    // Another cut before the split step makes a fourth piece: it stays in the body.
    const Uuid cutC = m.box("Cut C", {40, -1, -1}, {2, 12, 12});
    auto combine = std::make_unique<doc::CombineFeature>();
    combine->toolBody = cutC;
    combine->mode = doc::CombineMode::Subtract;
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(bar, std::move(combine), 3)).ok());
    EXPECT_FALSE(m.body(bar).hasFailures());
    EXPECT_EQ(m.body(bar).shape().solidCount(), 2);
    EXPECT_NEAR(m.volume(bar), 4600.0 - 200.0, 1e-6);
    EXPECT_NEAR(m.volume(piece2), 2800.0, 1e-6);
}

TEST(Split, FromTheSelectedBody)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    const Uuid b = h.addBox("Body 2", {30, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("union").ok()); // two separate boxes in one body
    ASSERT_EQ(h.document.body(a)->shape().solidCount(), 2);
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.hasAction("split"));
    bool flagged = false;
    for (const auto& row : h.controller.historyRows())
        flagged = flagged || (row.id == a && row.canSplit);
    EXPECT_TRUE(flagged) << "the body row offers the split";
    h.messages.clear();
    ASSERT_TRUE(h.controller.triggerAction("split").ok());
    // Body 1, its tool (Body 2, hidden), the piece's own hidden copy of that
    // tool, and the piece.
    ASSERT_EQ(h.document.bodies().size(), 4u);
    EXPECT_TRUE(h.saw("Split into 2 bodies.")) << (h.messages.empty() ? "" : h.messages.back());
    EXPECT_EQ(h.document.body(a)->shape().solidCount(), 1);
    const doc::Body& piece = *h.document.bodies().back();
    EXPECT_EQ(piece.name(), "Body 3");
    EXPECT_NEAR(geom::volume(piece.shape()), 1000.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(piece.shape()).min.x, 30.0, 1e-6);
    EXPECT_FALSE(h.document.bodies()[2]->isVisible());
    EXPECT_FALSE(h.hasAction("split"));
    bool detail = false, stillFlagged = false;
    for (const auto& row : h.controller.historyRows()) {
        detail = detail || (row.parentId == piece.id() && row.name == "Split" && row.detail == "Keeps 1 of 2 pieces");
        stillFlagged = stillFlagged || row.status == HistoryRow::Status::Warning || row.canSplit;
    }
    EXPECT_TRUE(detail);
    EXPECT_FALSE(stillFlagged) << "the splits dealt with the pieces the union left";
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 2u);
}

// Files are untrusted: malformed split params are refused.
TEST(Split, MalformedParamsAreRefused)
{
    doc::SplitFeature split;
    EXPECT_FALSE(split.readParams(nlohmann::json::parse(R"({"pieces": []})")).ok());
    EXPECT_FALSE(split.readParams(nlohmann::json::parse(
                                      R"({"pieces": [{"volume": 1, "centroid": [0,0,0], "min": [0,0,0], "max": [1,1,1]}]})"))
                     .ok())
        << "a split has at least two pieces";
    EXPECT_FALSE(split.readParams(nlohmann::json::parse(
                                      R"({"pieces": [{"volume": -1, "centroid": [0,0,0], "min": [0,0,0], "max": [1,1,1]},
                                                     {"volume": 1, "centroid": [0,0,0], "min": [0,0,0], "max": [1,1,1]}]})"))
                     .ok());
    doc::SplitPieceFeature piece;
    const std::string body = Uuid::generate().toString(), step = Uuid::generate().toString();
    EXPECT_TRUE(piece.readParams({{"body", body}, {"split", step}, {"piece", 1}}).ok());
    EXPECT_FALSE(piece.readParams({{"body", body}, {"split", step}, {"piece", 0}}).ok()) << "piece 0 stays in the body";
    EXPECT_FALSE(piece.readParams({{"body", "nope"}, {"split", step}, {"piece", 1}}).ok());
}

// ---- Split pieces in files from before independent pieces ----------------------------

// Such a piece (a body whose base SplitPiece step follows the body it was
// split off) still follows upstream edits, and fails with a clear message
// when the body is whole again.
TEST(Split, LegacyPieceFollowsAndFailsClearly)
{
    Model m;
    const SlottedPlate p = slottedPlate(m);
    ASSERT_TRUE(m.push(legacySplitCommand(m.document, p.plate)).ok());
    ASSERT_EQ(m.document.bodies().size(), 2u);
    const Uuid piece = m.document.bodies()[1]->id();
    EXPECT_EQ(m.body(piece).features().front()->kind(), doc::FeatureKind::SplitPiece);
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    m.setSketchDimension(p.slotSketch, p.slotWidth, 10);
    EXPECT_NEAR(m.volume(piece), 22 * 20 * 5.0, 1e-6) << "follows the wider slot";
    ASSERT_TRUE(m.stack.undo(m.document));

    const Uuid cut = m.body(p.plate).features()[1]->id();
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetFeatureSuppressedCommand>(cut, true)).ok());
    EXPECT_FALSE(m.body(p.plate).hasFailures());
    EXPECT_NEAR(m.volume(p.plate), 6000.0, 1e-6);
    ASSERT_TRUE(m.body(piece).hasFailures());
    EXPECT_NE(m.body(piece).state(0).userMessage.find("no longer separate"), std::string::npos)
        << m.body(piece).state(0).userMessage;
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_FALSE(m.body(piece).hasFailures());
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);

    // Suppressing the split itself puts the piece back into the body.
    const Uuid splitStep = m.body(p.plate).features().back()->id();
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetFeatureSuppressedCommand>(splitStep, true)).ok());
    EXPECT_NEAR(m.volume(p.plate), 5600.0, 1e-6);
    EXPECT_TRUE(m.body(piece).hasFailures());
    // Deleting the source body: the piece explains it.
    ASSERT_TRUE(m.stack.undo(m.document));
    ASSERT_TRUE(m.push(std::make_unique<cmd::DeleteBodyCommand>(p.plate)).ok());
    EXPECT_NE(m.body(piece).state(0).userMessage.find("no longer exists"), std::string::npos);
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_FALSE(m.body(piece).hasFailures());
}

// Duplicating such a piece: its source body stays shared, and the copy
// follows the source's upstream edits like the piece.
TEST(Split, DuplicatedLegacyPieceKeepsFollowingItsSource)
{
    Model m;
    const SlottedPlate p = slottedPlate(m);
    ASSERT_TRUE(m.push(legacySplitCommand(m.document, p.plate)).ok());
    const Uuid piece = m.document.bodies()[1]->id();
    auto command = std::make_unique<cmd::DuplicateBodyCommand>(piece);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());
    EXPECT_EQ(m.document.bodies().size(), 3u) << "the visible source is not copied";
    EXPECT_NEAR(m.volume(copy), 2800.0, 1e-6);
    m.setSketchDimension(p.slotSketch, p.slotWidth, 10);
    EXPECT_NEAR(m.volume(piece), 2200.0, 1e-6);
    EXPECT_NEAR(m.volume(copy), 2200.0, 1e-6);
}

// ---- Mirror / Pattern: separate, independent bodies ----------------------------------

namespace {
const HistoryRow* rowNamed(const std::vector<HistoryRow>& rows, const std::string& name)
{
    for (const auto& r : rows)
        if (r.name == name)
            return &r;
    return nullptr;
}

bool separateShown(const InteractionController& controller)
{
    for (const auto& action : controller.contextActions())
        if (action.id == "separate")
            return action.active;
    return false;
}
} // namespace

// A mirror image that would not touch the body becomes a body of its own (as
// in Shapr3D): a copy of the body's history ending in a Mirror step that keeps
// only the image. Pushing or moving the original afterwards leaves the image
// as it is, and the other way round.
TEST(Copies, MirrorAwayFromTheBodyMakesAnIndependentBody)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {5, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.hasAction("separate"));
    EXPECT_FALSE(separateShown(h.controller)) << "no plane yet: nothing decided";
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok()); // across YZ, 5 mm away
    const auto* mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->separate());
    EXPECT_TRUE(mirror->separateIsAutomatic());
    EXPECT_TRUE(separateShown(h.controller)) << "the toggle shows the automatic choice";
    EXPECT_TRUE(mirror->canCommit()) << mirror->error();
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_EQ(h.stack.undoLabel(), "Mirror");
    EXPECT_TRUE(h.saw("Mirrored as a separate body: the image does not touch the original."))
        << (h.messages.empty() ? "" : h.messages.back());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    const Uuid b = h.document.bodies().back()->id();
    EXPECT_EQ(h.document.body(b)->name(), "Body 2");
    EXPECT_EQ(h.document.body(a)->features().size(), 1u) << "the original gets no step";
    ASSERT_EQ(h.document.body(b)->features().size(), 2u);
    EXPECT_EQ(h.document.body(b)->features()[0]->kind(), doc::FeatureKind::Box);
    const auto* step = dynamic_cast<const doc::MirrorFeature*>(h.document.body(b)->features()[1].get());
    ASSERT_NE(step, nullptr);
    EXPECT_FALSE(step->keepOriginal);
    expectBox(geom::boundingBox(h.document.body(b)->shape()), {-15, 0, 0}, {-5, 10, 10});
    EXPECT_NEAR(geom::volume(h.document.body(b)->shape()), 1000.0, 1e-6);
    EXPECT_TRUE(h.document.bodiesUsing(a).empty()) << "the image is not built from the original";
    const HistoryRow* stepRow = h.row(step->id());
    ASSERT_NE(stepRow, nullptr);
    EXPECT_EQ(stepRow->name, "Mirror image");
    EXPECT_EQ(stepRow->detail, "Across YZ");

    // Push the original's +X face 10 mm, then move it up 30: the image stays.
    const doc::Body* source = h.document.body(a);
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(a, pushPull(source->shape(), {1, 0, 0}, 10)), h.document).ok());
    auto up = std::make_unique<doc::MoveFeature>();
    up->translation = {0, 0, 30};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(a, std::move(up)), h.document).ok());
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {5, 0, 30}, {25, 10, 40});
    expectBox(geom::boundingBox(h.document.body(b)->shape()), {-15, 0, 0}, {-5, 10, 10});
    EXPECT_NEAR(geom::volume(h.document.body(b)->shape()), 1000.0, 1e-6);
    // The image's own box made taller: the original stays.
    ASSERT_TRUE(h.controller.setFeatureParameter(h.document.body(b)->features()[0]->id(), "height", "25").ok());
    expectBox(geom::boundingBox(h.document.body(b)->shape()), {-15, 0, 0}, {-5, 10, 25});
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {5, 0, 30}, {25, 10, 40});
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 2000.0, 1e-6);

    // The mirror is one undo step; redo brings the same body back.
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 2u);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
    ASSERT_TRUE(h.controller.redo());
    ASSERT_NE(h.document.body(b), nullptr);
    expectBox(geom::boundingBox(h.document.body(b)->shape()), {-15, 0, 0}, {-5, 10, 10});
    // A save keeps the image step (the plane under "plane", keepOriginal false).
    const auto path = tempPath("mirror_image.openshape");
    ASSERT_TRUE(io::saveProject(h.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    const doc::Body* reread = loaded.value()->body(b);
    ASSERT_NE(reread, nullptr);
    const auto* rereadStep = dynamic_cast<const doc::MirrorFeature*>(reread->features().back().get());
    ASSERT_NE(rereadStep, nullptr);
    EXPECT_FALSE(rereadStep->keepOriginal);
    expectBox(geom::boundingBox(reread->shape()), {-15, 0, 0}, {-5, 10, 10});
    std::filesystem::remove(path);
}

// Across a plane the body touches (its own face): the image joins the body,
// one symmetric solid (as before).
TEST(Copies, MirrorAcrossItsOwnFaceJoins)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok()); // YZ: the box's -X face
    const auto* mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->separate());
    EXPECT_FALSE(separateShown(h.controller));
    h.messages.clear();
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_FALSE(h.saw("separate body"));
    ASSERT_EQ(h.document.bodies().size(), 1u);
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {-10, 0, 0}, {10, 10, 10});
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 2000.0, 1e-6);
    EXPECT_EQ(h.document.body(a)->shape().solidCount(), 1);
    const auto* step = dynamic_cast<const doc::MirrorFeature*>(h.document.body(a)->features().back().get());
    ASSERT_NE(step, nullptr);
    EXPECT_TRUE(step->keepOriginal);
}

// The toggle wins over the automatic choice both ways: an image touching the
// body made a separate body, an image apart from it joined (one body in two
// pieces, with the hint to split it).
TEST(Copies, ToggleForcesSeparateOrJoined)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok()); // joined -> separate
    const auto* mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->separate());
    EXPECT_FALSE(mirror->separateIsAutomatic());
    // Chosen, it stays so when the plane changes.
    ASSERT_TRUE(h.controller.triggerAction("plane:2").ok()); // XY: the bottom face
    mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->separate());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok());
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_TRUE(h.saw("Mirrored as a separate body."));
    ASSERT_EQ(h.document.bodies().size(), 2u);
    expectBox(geom::boundingBox(h.document.bodies()[1]->shape()), {-10, 0, 0}, {0, 10, 10});
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 1000.0, 1e-6);
    EXPECT_TRUE(h.controller.undo());

    // Apart from the body, turned off: joined into one body in two pieces.
    const Uuid far = h.addBox("Body 3", {50, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(far, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok());
    mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_TRUE(mirror && mirror->separateIsAutomatic());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok()); // separate -> joined
    mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->separate());
    EXPECT_FALSE(separateShown(h.controller));
    h.messages.clear();
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_EQ(h.document.body(far)->shape().solidCount(), 2);
    EXPECT_NEAR(geom::volume(h.document.body(far)->shape()), 2000.0, 1e-6);
    EXPECT_TRUE(h.saw("Split into bodies")) << (h.messages.empty() ? "" : h.messages.back());
}

// Pattern: three copies 5 mm apart do not touch, so they are three bodies;
// each copy is the box's history plus a Move step. Editing or moving the
// original leaves the copies alone, and the other way round.
TEST(Copies, PatternWithGapsGivesIndependentBodies)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0)..(10,10,20)
    const Uuid a = h.document.bodies().front()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern->separate());
    EXPECT_TRUE(pattern->separateIsAutomatic());
    EXPECT_TRUE(pattern->hasPreview());
    EXPECT_TRUE(separateShown(h.controller));
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.stack.undoLabel(), "Pattern");
    EXPECT_TRUE(h.saw("Patterned as 2 separate bodies: the copies do not touch the original."))
        << (h.messages.empty() ? "" : h.messages.back());
    ASSERT_EQ(h.document.bodies().size(), 3u);
    EXPECT_EQ(h.document.body(a)->features().size(), 1u);
    for (std::size_t i = 0; i < 3; ++i) {
        const doc::Body& body = *h.document.bodies()[i];
        expectBox(geom::boundingBox(body.shape()), {-10.0 + 25.0 * double(i), -10, 0}, {10.0 + 25.0 * double(i), 10, 20});
        EXPECT_NEAR(geom::volume(body.shape()), 8000.0, 1e-6) << i;
        if (i > 0) {
            EXPECT_EQ(body.name(), "Body " + std::to_string(i + 1));
            ASSERT_EQ(body.features().size(), 2u);
            EXPECT_EQ(body.features()[1]->kind(), doc::FeatureKind::Move);
            EXPECT_EQ(body.features()[1]->name(), "Pattern copy");
        }
    }
    EXPECT_TRUE(h.document.bodiesUsing(a).empty());
    // The source stays selected (with plain Move arrows).
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].bodyId, a);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Move");

    // The original taller, then pushed and moved: every copy stays.
    const Uuid copy1 = h.document.bodies()[1]->id(), copy2 = h.document.bodies()[2]->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(h.document.body(a)->features()[0]->id(), "height", "30").ok());
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(a, pushPull(h.document.body(a)->shape(), {0, 1, 0}, 5)),
                             h.document)
                    .ok());
    auto away = std::make_unique<doc::MoveFeature>();
    away->translation = {0, 100, 0};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(a, std::move(away)), h.document).ok());
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {-10, 90, 0}, {10, 115, 30});
    expectBox(geom::boundingBox(h.document.body(copy1)->shape()), {15, -10, 0}, {35, 10, 20});
    expectBox(geom::boundingBox(h.document.body(copy2)->shape()), {40, -10, 0}, {60, 10, 20});
    // A copy's own Move step edited: only that copy moves.
    ASSERT_TRUE(h.controller.setFeatureParameter(h.document.body(copy2)->features()[1]->id(), "x", "80").ok());
    expectBox(geom::boundingBox(h.document.body(copy2)->shape()), {70, -10, 0}, {90, 10, 20});
    expectBox(geom::boundingBox(h.document.body(copy1)->shape()), {15, -10, 0}, {35, 10, 20});
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 20 * 25 * 30.0, 1e-6);

    // Save and reopen: the copies are still independent.
    const auto path = tempPath("copies.openshape");
    ASSERT_TRUE(io::saveProject(h.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_EQ(d.bodies().size(), 3u);
    expectBox(geom::boundingBox(d.body(copy2)->shape()), {70, -10, 0}, {90, 10, 20});
    const Uuid loadedBox = d.body(a)->features()[0]->id();
    ASSERT_TRUE(d.body(a)->feature(loadedBox)->setParameter("width", 40.0).ok());
    d.featureChanged(loadedBox);
    EXPECT_NEAR(geom::volume(d.body(copy1)->shape()), 8000.0, 1e-6);
    std::filesystem::remove(path);

    // The pattern is one undo step.
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 3u);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
    ASSERT_TRUE(h.controller.redo());
    ASSERT_EQ(h.document.bodies().size(), 3u);
    expectBox(geom::boundingBox(h.document.body(copy2)->shape()), {40, -10, 0}, {60, 10, 20});
}

// Copies that touch (spacing = the body's size) join into one body, as before.
TEST(Copies, PatternTouchingJoins)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    const Uuid a = h.document.bodies().front()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    EXPECT_EQ(h.controller.setValueText("20"), "");
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_FALSE(pattern->separate());
    EXPECT_FALSE(separateShown(h.controller));
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {-10, -10, 0}, {50, 10, 20});
    EXPECT_EQ(h.document.body(a)->shape().solidCount(), 1);
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 3 * 8000.0, 1e-3);
}

// A circular pattern as separate bodies (chosen: the turned copies overlap
// the original): each copy's Move step turns it; a file round trip keeps them.
TEST(Copies, CircularPatternAsSeparateBodies)
{
    Harness h;
    const Uuid bar = h.addBox("Bar", {5, -1, 0}, {10, 2, 2});
    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("layout:circular").ok());
    ASSERT_TRUE(h.controller.triggerAction("fewer").ok());
    ASSERT_TRUE(h.controller.triggerAction("fewer").ok()); // 4 in total, 90 degrees apart
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_TRUE(pattern && !pattern->separate()) << "turned about its own center, the copies overlap it";
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_TRUE(h.saw("Patterned as 3 separate bodies."));
    ASSERT_EQ(h.document.bodies().size(), 4u);
    const auto* move = dynamic_cast<const doc::MoveFeature*>(h.document.bodies()[2]->features().back().get());
    ASSERT_NE(move, nullptr);
    EXPECT_TRUE(move->rotates);
    EXPECT_NEAR(std::abs(move->rotationAngle), kPi, 1e-9);
    for (std::size_t i = 1; i < 4; ++i)
        EXPECT_NEAR(geom::volume(h.document.bodies()[i]->shape()), 40.0, 1e-6);
    // Half a turn about the bar's center (10, 0): it lands on itself.
    expectBox(geom::boundingBox(h.document.bodies()[2]->shape()), {5, -1, 0}, {15, 1, 2});
    const auto path = tempPath("circular.openshape");
    ASSERT_TRUE(io::saveProject(h.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    const auto* reread = dynamic_cast<const doc::MoveFeature*>(loaded.value()->bodies()[1]->features().back().get());
    ASSERT_NE(reread, nullptr);
    EXPECT_NEAR(reread->rotationAngle, kPi / 2, 1e-12);
    const auto x = geom::boundingBox(h.document.bodies()[1]->shape());
    const auto y = geom::boundingBox(loaded.value()->bodies()[1]->shape());
    EXPECT_NEAR((x.min - y.min).length() + (x.max - y.max).length(), 0.0, 1e-9);
    expectBox(y, {9, -5, 0}, {11, 5, 2});
    std::filesystem::remove(path);
}

// Independent copies can be subtracted from their source (no dependency).
TEST(Copies, CopyCanBeSubtractedFromItsSource)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    const Uuid b = h.document.bodies().back()->id();
    auto move = std::make_unique<doc::MoveFeature>();
    move->translation = {5, 0, 5}; // halfway into the original
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(b, std::move(move)), h.document).ok());
    h.controller.documentChanged();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 1000.0 - 5 * 10 * 5.0, 1e-6);
    EXPECT_FALSE(h.document.body(b)->isVisible());
}

TEST(Copies, TooManySeparateBodiesAreRefused)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {1, 1, 1});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    auto* pattern = const_cast<PatternOperation*>(dynamic_cast<const PatternOperation*>(h.controller.operation()));
    ASSERT_NE(pattern, nullptr);
    ASSERT_TRUE(pattern->separateIsAutomatic());
    // More copies than separate bodies allow: automatically joined instead.
    pattern->setCount(102, h.document);
    EXPECT_FALSE(pattern->separate());
    EXPECT_TRUE(pattern->canCommit()) << pattern->error();
    // Chosen, they are refused.
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    pattern = const_cast<PatternOperation*>(dynamic_cast<const PatternOperation*>(h.controller.operation()));
    ASSERT_TRUE(pattern && pattern->separate());
    EXPECT_FALSE(pattern->canCommit());
    EXPECT_NE(pattern->error().find("up to 100"), std::string::npos) << pattern->error();
    pattern->setCount(101, h.document);
    EXPECT_TRUE(pattern->canCommit()) << pattern->error();
}

// Files are untrusted: malformed Copy and Mirror params are refused.
TEST(Copies, MalformedParamsAreRefused)
{
    doc::CopyFeature copy;
    const std::string body = Uuid::generate().toString();
    EXPECT_TRUE(copy.readParams({{"body", body}, {"translation", {1, 2, 3}}}).ok());
    EXPECT_FALSE(copy.mirror);
    EXPECT_TRUE(copy.readParams({{"body", body}, {"mirror", {{"origin", {0, 0, 0}}, {"normal", {1, 0, 0}}}}}).ok());
    EXPECT_TRUE(copy.mirror);
    EXPECT_FALSE(copy.readParams({{"body", body}, {"mirror", {{"origin", {0, 0, 0}}, {"normal", {0, 0, 0}}}}}).ok());
    EXPECT_FALSE(copy.readParams({{"body", body}}).ok());
    EXPECT_FALSE(copy.readParams({{"body", body}, {"translation", {0, 0, 0}}, {"rotation", {{"center", {0, 0, 0}}, {"angle", 1.0}}}})
                     .ok())
        << "a rotation needs an axis";

    using nlohmann::json;
    doc::MirrorFeature mirror;
    const json plane{{"origin", {1, 2, 3}}, {"normal", {0, 0, 1}}};
    ASSERT_TRUE(mirror.readParams(plane).ok());
    EXPECT_TRUE(mirror.keepOriginal) << "files without the field join";
    ASSERT_TRUE(mirror.readParams({{"keepOriginal", false}, {"plane", plane}}).ok());
    EXPECT_FALSE(mirror.keepOriginal);
    EXPECT_NEAR(mirror.planeOrigin.z, 3.0, 1e-12);
    json written;
    mirror.writeParams(written);
    EXPECT_FALSE(written.contains("origin")) << "older builds must refuse it, not join";
    EXPECT_EQ(written["keepOriginal"], false);
    ASSERT_TRUE(mirror.readParams(plane).ok());
    EXPECT_TRUE(mirror.keepOriginal);
    EXPECT_FALSE(mirror.readParams({{"keepOriginal", "no"}, {"plane", plane}}).ok());
    EXPECT_FALSE(mirror.readParams({{"keepOriginal", false}}).ok()) << "no plane";
    EXPECT_FALSE(mirror.readParams({{"keepOriginal", false}, {"origin", {0, 0, 0}}, {"normal", {1, 0, 0}}}).ok())
        << "the image's plane goes under \"plane\"";
    EXPECT_FALSE(mirror.readParams({{"keepOriginal", false}, {"plane", {{"origin", {0, 0, 0}}, {"normal", {0, 0, 0}}}}}).ok());
    EXPECT_FALSE(mirror.readParams({{"origin", {0, 0, 0}}, {"normal", {1, 0, 0}}, {"plane", plane}}).ok());
}

// ---- Separate copies in files from before independent copies --------------------------

// A copy whose base Copy step follows its source (the "Separate bodies"
// option before copies became independent): it still follows the source.
TEST(Copies, LegacyCopyFollowsItsSource)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {5, 0, 0}, {10, 10, 10});
    auto mirror = std::make_unique<doc::CopyFeature>();
    mirror->sourceBody = a;
    mirror->mirror = true;
    mirror->planeOrigin = {0, 0, 0};
    mirror->planeNormal = {1, 0, 0};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>("Body 2", std::move(mirror)), h.document).ok());
    h.controller.documentChanged();
    const Uuid b = h.document.bodies().back()->id();
    const HistoryRow* copyRow = rowNamed(h.controller.historyRows(), "Mirror copy");
    ASSERT_NE(copyRow, nullptr);
    EXPECT_EQ(copyRow->detail, "Of Body 1 \xC2\xB7 Across YZ");
    ASSERT_TRUE(h.controller.setFeatureParameter(h.document.body(a)->features()[0]->id(), "width", "20").ok());
    EXPECT_NEAR(geom::boundingBox(h.document.body(b)->shape()).min.x, -25.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.document.body(b)->shape()), 2000.0, 1e-6);
}

// A copy is built from its source, so the source cannot take it as a tool;
// a union of the two still works (the copy keeps the result).
TEST(Copies, UnionOfALegacyCopyWithItsSource)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    auto copy = std::make_unique<doc::CopyFeature>();
    copy->sourceBody = a;
    copy->mirror = true;
    copy->planeOrigin = {10, 0, 0};
    copy->planeNormal = {1, 0, 0};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>("Body 2", std::move(copy)), h.document).ok());
    h.controller.documentChanged();
    const Uuid b = h.document.bodies().back()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    EXPECT_FALSE(h.controller.triggerAction("subtract").ok()) << "the source cannot cut its own copy away";
    ASSERT_TRUE(h.controller.triggerAction("union").ok());
    EXPECT_FALSE(h.document.body(a)->isVisible());
    EXPECT_NEAR(geom::volume(h.document.body(b)->shape()), 2000.0, 1e-6);
    EXPECT_EQ(h.document.body(b)->shape().solidCount(), 1);
    EXPECT_FALSE(h.document.body(b)->hasFailures());
}

// tests/data/legacy-copies-and-pieces.openshape was saved by the build before
// independent copies (2026-09-26, feee7aa): Split into bodies (a SplitPiece
// body) and Mirror / Pattern with Separate bodies (Copy bodies, one more with
// a rotation). It loads with the same volumes and boxes, and the old links
// still work: the copies follow their source.
TEST(Copies, LegacyFileLoadsAsBefore)
{
    auto loaded = io::loadProject(std::filesystem::path(OPENSHAPE_TEST_DATA_DIR) / "legacy-copies-and-pieces.openshape");
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_EQ(d.bodies().size(), 7u);
    struct Expected {
        const char* name;
        doc::FeatureKind base;
        double volume;
        Vec3 min, max;
    };
    const Expected expected[] = {
        {"Body 1", doc::FeatureKind::Box, 3600, {24, 0, 0}, {60, 20, 5}},
        {"Body 2", doc::FeatureKind::SplitPiece, 2000, {0, 0, 0}, {20, 20, 5}},
        {"Body 3", doc::FeatureKind::Box, 1500, {100, 0, 0}, {110, 10, 15}},
        {"Body 4", doc::FeatureKind::Copy, 1500, {-110, 0, 0}, {-100, 10, 15}},
        {"Body 5", doc::FeatureKind::Copy, 1500, {115, 0, 0}, {125, 10, 15}},
        {"Body 6", doc::FeatureKind::Copy, 1500, {130, 0, 0}, {140, 10, 15}},
        {"Body 7", doc::FeatureKind::Copy, 1500, {90, -30, 0}, {100, -20, 15}},
    };
    for (std::size_t i = 0; i < 7; ++i) {
        const doc::Body& body = *d.bodies()[i];
        EXPECT_EQ(body.name(), expected[i].name);
        EXPECT_EQ(body.features().front()->kind(), expected[i].base) << body.name();
        EXPECT_FALSE(body.hasFailures()) << body.name();
        EXPECT_NEAR(geom::volume(body.shape()), expected[i].volume, 1e-6) << body.name();
        expectBox(geom::boundingBox(body.shape()), expected[i].min, expected[i].max);
    }
    // The old links: a taller Body 3 makes every copy taller; a wider slot
    // (the plate's sketch) makes the split-off piece shorter.
    const Uuid box = d.bodies()[2]->features()[0]->id();
    ASSERT_TRUE(d.body(d.bodies()[2]->id())->feature(box)->setParameter("height", 25.0).ok());
    d.featureChanged(box);
    for (std::size_t i = 3; i < 7; ++i)
        EXPECT_NEAR(geom::volume(d.bodies()[i]->shape()), 2500.0, 1e-6) << d.bodies()[i]->name();
    EXPECT_TRUE(d.bodiesUsing(d.bodies()[2]->id()).size() == 4u);
    EXPECT_EQ(d.bodiesUsing(d.bodies()[0]->id()).size(), 1u);
}

// ---- Rotate about a picked edge or point ------------------------------------------

// Clicking a straight edge makes it the axis: a 20 x 10 x 5 box turned 90
// degrees about its top front edge stands up on that edge.
TEST(RotateAbout, EdgeGivesTheExactBox)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {20, 10, 5});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("rotate").ok());
    h.clickAt(h.screen({10, 0, 5})); // the top front edge, along X
    const auto* rotate = dynamic_cast<const RotateOperation*>(h.controller.operation());
    ASSERT_NE(rotate, nullptr) << "clicking an edge keeps rotating";
    ASSERT_TRUE(rotate->axis().has_value());
    EXPECT_NEAR(rotate->axis()->x, 1.0, 1e-12) << "the axis points along +X";
    EXPECT_NEAR((rotate->center() - Vec3{10, 0, 5}).length(), 0.0, 1e-9);
    EXPECT_EQ(rotate->ringCount(), 1);
    EXPECT_EQ(rotate->valueLabel(), "Angle");
    EXPECT_EQ(h.controller.renderScene().rings.size(), 1u);
    EXPECT_EQ(h.controller.renderScene().rings[0].axis, 0) << "colored as X";
    EXPECT_EQ(h.controller.setValueText("90"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {0, 0, 5}, {20, 5, 15});
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 1000.0, 1e-6);
    bool detail = false;
    for (const auto& row : h.controller.historyRows())
        detail = detail || (row.name == "Rotate" && row.detail == "90.0\xC2\xB0 about X");
    EXPECT_TRUE(detail);
    EXPECT_TRUE(h.controller.undo());
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {0, 0, 0}, {20, 10, 5});
}

// Clicking an edge near its end picks that corner: the X/Y/Z rings move there.
TEST(RotateAbout, CornerMovesThePivot)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {20, 10, 5});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("rotate").ok());
    const Vec2 corner = h.screen({20, 0, 5});
    h.clickAt(corner + Vec2{-3, 2}); // a few pixels off the corner, on the box's edges
    const auto* rotate = dynamic_cast<const RotateOperation*>(h.controller.operation());
    ASSERT_NE(rotate, nullptr);
    EXPECT_FALSE(rotate->axis().has_value());
    EXPECT_NEAR((rotate->center() - Vec3{20, 0, 5}).length(), 0.0, 1e-9);
    EXPECT_EQ(rotate->ringCount(), 3);
    EXPECT_EQ(rotate->valueLabel(), "Angle Z");
    EXPECT_TRUE(h.hasAction("pivotCenter"));
    EXPECT_EQ(h.controller.setValueText("90"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    // 90 degrees about Z through (20, 0): x' = 20 - y, y' = x - 20.
    expectBox(geom::boundingBox(h.document.body(a)->shape()), {10, -20, 0}, {20, 0, 5});
}

// A ring is grabbed when dragged; a click on it (away from edges) makes it the
// active ring, as before.
TEST(RotateAbout, ClickingARingActivatesIt)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {20, 10, 5});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("rotate").ok());
    const Operation* op = h.controller.operation();
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->activeHandle(), 2);
    const Camera& cam = h.controller.camera();
    std::optional<Vec2> spot;
    for (int k = 0; k < 36 && !spot; ++k) {
        const Vec2 p = cam.project(op->ring(0).pointAt(cam, 2 * kPi * k / 36));
        if (h.controller.pickAt(p, InputProfile{}).kind != sel::PickKind::Edge)
            spot = p;
    }
    ASSERT_TRUE(spot.has_value());
    h.clickAt(*spot);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->activeHandle(), 0);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Angle X");
    EXPECT_EQ(h.document.bodies().size(), 1u);
}

TEST(RotateAbout, CircleCenterAndBackToTheBodyCenter)
{
    Model m;
    sketch::Sketch s(Uuid::generate(), sketch::Plane::xy());
    s.setName("Sketch 1");
    s.addCircle(s.addPoint({30, 0}), 5);
    const Uuid sketchId = s.id();
    ASSERT_TRUE(m.push(std::make_unique<cmd::CreateSketchCommand>(std::move(s))).ok());
    const Uuid disk = m.extrude(sketchId, 10);
    cmd::UndoStack& stack = m.stack;
    InteractionController controller(m.document, stack);
    controller.setViewportSize({1200, 800});
    controller.fitAll(false);
    ASSERT_TRUE(controller.selectBody(disk, false).ok());
    ASSERT_TRUE(controller.runTool("rotate").ok());
    const Vec2 rim = controller.camera().project({30 + 5 * std::cos(-1.2), 5 * std::sin(-1.2), 10});
    controller.pointerPress(Harness::at(rim));
    controller.pointerRelease(Harness::at(rim));
    const auto* rotate = dynamic_cast<const RotateOperation*>(controller.operation());
    ASSERT_NE(rotate, nullptr);
    EXPECT_NEAR((rotate->center() - Vec3{30, 0, 10}).length(), 0.0, 1e-6) << "the top rim's center";
    // The round side: its axis (vertical, through the disk's center).
    const Vec2 side = controller.camera().project({30 + 5 * std::cos(-1.0), 5 * std::sin(-1.0), 4});
    controller.pointerPress(Harness::at(side));
    controller.pointerRelease(Harness::at(side));
    rotate = dynamic_cast<const RotateOperation*>(controller.operation());
    ASSERT_NE(rotate, nullptr);
    ASSERT_TRUE(rotate->axis().has_value());
    EXPECT_NEAR(rotate->axis()->z, 1.0, 1e-9);
    EXPECT_NEAR(rotate->center().x, 30.0, 1e-6);
    EXPECT_NEAR(rotate->center().y, 0.0, 1e-6);
    ASSERT_TRUE(controller.triggerAction("pivotCenter").ok());
    rotate = dynamic_cast<const RotateOperation*>(controller.operation());
    ASSERT_NE(rotate, nullptr);
    EXPECT_FALSE(rotate->hasCustomPivot());
    EXPECT_NEAR(rotate->center().z, 5.0, 1e-3) << "the body's center again";
}

// A step that leaves the body in pieces says how to split it (a mirror image
// apart from the body, joined because Separate bodies was turned off).
TEST(Split, MirrorThatLeavesPiecesSuggestsTheSplit)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {5, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok()); // off: join
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("Split into bodies"), std::string::npos) << h.messages.back();
}

// Subtracting a slot body that cuts a plate in two says how to split it (the
// Combine path, not only tool operations); a union of bodies that do not
// touch was meant to hold them together and gets no hint.
TEST(Split, SubtractThatCutsInTwoSuggestsTheSplit)
{
    Harness h;
    const Uuid plate = h.addBox("Body 1", {0, 0, 0}, {60, 20, 5});
    const Uuid slot = h.addBox("Slot", {28, -5, -5}, {4, 30, 15});
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.selectBody(slot, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    ASSERT_EQ(h.document.body(plate)->shape().solidCount(), 2);
    EXPECT_TRUE(h.saw("Body 1 is now in 2 separate pieces")) << (h.messages.empty() ? "" : h.messages.back());
    EXPECT_TRUE(h.saw("Split into bodies"));

    h.messages.clear();
    const Uuid a = h.addBox("Body 3", {100, 0, 0}, {5, 5, 5});
    const Uuid b = h.addBox("Body 4", {120, 0, 0}, {5, 5, 5});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("union").ok());
    ASSERT_EQ(h.document.body(a)->shape().solidCount(), 2);
    EXPECT_FALSE(h.saw("Split into bodies"));
}

// The Model panel's Split button reports why nothing happens while a sketch
// is being edited (it ignores the returned Status).
TEST(Split, InSketchModeSaysWhy)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    const Uuid b = h.addBox("Body 2", {30, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("union").ok());
    h.controller.keyPress(Key::Escape);
    h.controller.keyPress(Key::Escape);
    ASSERT_TRUE(h.controller.selection().empty());
    ASSERT_TRUE(h.controller.startSketch().ok());
    ASSERT_EQ(h.controller.mode(), InteractionController::Mode::Sketch);
    h.messages.clear();
    EXPECT_FALSE(h.controller.splitBody(a).ok());
    EXPECT_TRUE(h.saw("Finish the sketch first."));
    EXPECT_EQ(h.document.body(a)->shape().solidCount(), 2);
}

// ---- Deleting a body others are built from ------------------------------------------

// In a file from before independent pieces, deleting the body a piece was
// split off would break the piece: the body is hidden instead (one undo step)
// and the piece stays exactly as it was. Deleting both at once deletes both.
TEST(Delete, LegacySplitParentIsHiddenNotDeleted)
{
    Harness h;
    const Uuid plate = h.addBox("Body 1", {0, 0, 0}, {60, 20, 5});
    const Uuid slot = h.addBox("Slot", {28, -5, -5}, {4, 30, 15});
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.selectBody(slot, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    ASSERT_TRUE(h.stack.push(legacySplitCommand(h.document, plate), h.document).ok());
    h.controller.documentChanged();
    ASSERT_EQ(h.document.bodies().size(), 3u);
    const Uuid piece = h.document.bodies().back()->id();
    ASSERT_EQ(h.document.body(piece)->name(), "Body 2");
    EXPECT_NEAR(geom::volume(h.document.body(piece)->shape()), 2800.0, 1e-6);

    // The Model panel says it.
    const HistoryRow* r = h.row(plate);
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->canDelete);
    EXPECT_NE(r->message.find("Body 2 is built from it"), std::string::npos) << r->message;

    // Delete in the view: hidden, not deleted; the piece is unchanged.
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    h.messages.clear();
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    ASSERT_NE(h.document.body(plate), nullptr);
    EXPECT_FALSE(h.document.body(plate)->isVisible());
    EXPECT_EQ(h.document.bodies().size(), 3u);
    EXPECT_FALSE(h.document.body(piece)->hasFailures());
    EXPECT_NEAR(geom::volume(h.document.body(piece)->shape()), 2800.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(h.document.body(piece)->shape()).min.x, 32.0, 1e-6);
    EXPECT_TRUE(h.saw("Body 1 is hidden, not deleted: Body 2 is built from it."))
        << (h.messages.empty() ? "" : h.messages.back());
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.stack.undoLabel(), "Hide body");

    // Hidden, the Model panel offers no Delete, and a delete is refused.
    r = h.row(plate);
    ASSERT_NE(r, nullptr);
    EXPECT_FALSE(r->canDelete);
    EXPECT_NE(r->message.find("Kept hidden"), std::string::npos) << r->message;
    const Status refused = h.controller.deleteBody(plate);
    EXPECT_FALSE(refused.ok());
    EXPECT_EQ(refused.userMessage(), "Body 1 cannot be deleted: Body 2 is built from it.");
    EXPECT_EQ(h.document.bodies().size(), 3u);

    // One undo shows it again.
    EXPECT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.document.body(plate)->isVisible());

    // The plate and its piece together: both go, in one undo step.
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.selectBody(piece, true).ok());
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.document.body(plate), nullptr);
    EXPECT_EQ(h.document.body(piece), nullptr);
    EXPECT_EQ(h.document.bodies().size(), 1u) << "the slot (a hidden tool) stays";
    EXPECT_EQ(h.stack.undoLabel(), "Delete bodies");
    EXPECT_TRUE(h.controller.undo());
    ASSERT_EQ(h.document.bodies().size(), 3u);
    EXPECT_FALSE(h.document.body(piece)->hasFailures());
    EXPECT_NEAR(geom::volume(h.document.body(piece)->shape()), 2800.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.document.body(plate)->shape()), 2800.0, 1e-6);

    // The piece alone: nothing is built from it, so it is deleted.
    ASSERT_TRUE(h.controller.selectBody(piece, false).ok());
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.document.body(piece), nullptr);
    EXPECT_TRUE(h.document.body(plate)->isVisible());
    EXPECT_NEAR(geom::volume(h.document.body(plate)->shape()), 2800.0, 1e-6);
    EXPECT_EQ(h.stack.undoLabel(), "Delete body");
}

// Of a body and one of its two split-off pieces (a file from before
// independent pieces), the piece is deleted and the body hidden (the other
// piece is built from it); the message names only the body that stays.
TEST(Delete, LegacyParentAndOneOfTwoPieces)
{
    Harness h;
    const Uuid bar = h.addBox("Body 1", {0, 0, 0}, {100, 10, 10});
    const Uuid cutA = h.addBox("Cut A", {20, -1, -1}, {2, 12, 12});
    const Uuid cutB = h.addBox("Cut B", {70, -1, -1}, {2, 12, 12});
    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.selectBody(cutA, true).ok());
    ASSERT_TRUE(h.controller.selectBody(cutB, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    ASSERT_EQ(h.document.body(bar)->shape().solidCount(), 3);
    EXPECT_TRUE(h.saw("Body 1 is now in 3 separate pieces"));
    ASSERT_TRUE(h.stack.push(legacySplitCommand(h.document, bar), h.document).ok());
    h.controller.documentChanged();
    ASSERT_EQ(h.document.bodies().size(), 5u);
    const Uuid pieceA = h.document.bodies()[3]->id(); // "Body 2", 28 mm
    const Uuid pieceB = h.document.bodies()[4]->id(); // "Body 3", 20 mm

    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.selectBody(pieceA, true).ok());
    h.messages.clear();
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.document.body(pieceA), nullptr);
    ASSERT_NE(h.document.body(bar), nullptr);
    EXPECT_FALSE(h.document.body(bar)->isVisible());
    EXPECT_FALSE(h.document.body(pieceB)->hasFailures());
    EXPECT_NEAR(geom::volume(h.document.body(pieceB)->shape()), 2000.0, 1e-6);
    EXPECT_TRUE(h.saw("Body 1 is hidden, not deleted: Body 3 is built from it."))
        << (h.messages.empty() ? "" : h.messages.back());
    EXPECT_EQ(h.stack.undoLabel(), "Delete");
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 5u);
    EXPECT_TRUE(h.document.body(bar)->isVisible());
    EXPECT_NEAR(geom::volume(h.document.body(pieceA)->shape()), 2800.0, 1e-6);
}

// The same for the source of separate copies in a file from before
// independent copies (bodies whose base Copy step follows it).
TEST(Delete, LegacyCopySourceIsHiddenNotDeleted)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    const Uuid a = h.document.bodies().front()->id();
    for (int i = 1; i < 3; ++i) {
        auto copy = std::make_unique<doc::CopyFeature>();
        copy->sourceBody = a;
        copy->motion.translation = {25.0 * i, 0, 0};
        ASSERT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>(h.document.nextBodyName(), std::move(copy)), h.document)
                        .ok());
    }
    h.controller.documentChanged();
    ASSERT_EQ(h.document.bodies().size(), 3u);

    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    h.messages.clear();
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    ASSERT_EQ(h.document.bodies().size(), 3u);
    EXPECT_FALSE(h.document.body(a)->isVisible());
    for (std::size_t i = 1; i < 3; ++i) {
        EXPECT_FALSE(h.document.bodies()[i]->hasFailures()) << i;
        EXPECT_NEAR(geom::volume(h.document.bodies()[i]->shape()), 8000.0, 1e-6) << i;
        EXPECT_NEAR(geom::boundingBox(h.document.bodies()[i]->shape()).min.x, -10.0 + 25.0 * double(i), 1e-6) << i;
    }
    EXPECT_TRUE(h.saw("Body 1 is hidden, not deleted: Body 2 and Body 3 are built from it."))
        << (h.messages.empty() ? "" : h.messages.back());
    EXPECT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.document.body(a)->isVisible());

    // The Model panel's Delete on the visible source hides it too.
    ASSERT_TRUE(h.controller.deleteBody(a).ok());
    EXPECT_FALSE(h.document.body(a)->isVisible());
    EXPECT_EQ(h.document.bodies().size(), 3u);
}

// Copies made by Pattern and pieces made by Split into bodies are independent:
// the body they came from is deleted like any other, and they stay.
TEST(Delete, SourceOfIndependentCopiesAndPiecesIsDeleted)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    const Uuid a = h.document.bodies().front()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.document.bodies().size(), 3u);
    const HistoryRow* r = h.row(a);
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->canDelete);
    EXPECT_TRUE(r->message.empty()) << r->message;
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.document.body(a), nullptr);
    ASSERT_EQ(h.document.bodies().size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_FALSE(h.document.bodies()[i]->hasFailures()) << i;
        EXPECT_NEAR(geom::volume(h.document.bodies()[i]->shape()), 8000.0, 1e-6) << i;
        EXPECT_NEAR(geom::boundingBox(h.document.bodies()[i]->shape()).min.x, 15.0 + 25.0 * double(i), 1e-6) << i;
    }
    EXPECT_EQ(h.stack.undoLabel(), "Delete body");
    EXPECT_TRUE(h.controller.undo());
    ASSERT_EQ(h.document.bodies().size(), 3u);

    // A plate cut in two, split: the plate is deleted, the piece stays.
    const Uuid plate = h.addBox("Plate", {0, 100, 0}, {60, 20, 5});
    const Uuid slot = h.addBox("Slot", {28, 95, -5}, {4, 30, 15});
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.selectBody(slot, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("split").ok());
    const Uuid piece = h.document.bodies().back()->id();
    EXPECT_NEAR(geom::volume(h.document.body(piece)->shape()), 2800.0, 1e-6);
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.document.body(plate), nullptr);
    EXPECT_FALSE(h.document.body(piece)->hasFailures());
    EXPECT_NEAR(geom::volume(h.document.body(piece)->shape()), 2800.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(h.document.body(piece)->shape()).min.x, 32.0, 1e-6);
}

// ---- Duplicate: what is copied depends on the kind of reference ----------------------

// A mirror copy's source that is merely hidden is still shared: the duplicate
// follows it like the copy does.
TEST(Duplicate, HiddenSourceOfACopyStaysShared)
{
    Model m;
    const Uuid a = m.box("Body 1", {5, 0, 0}, {10, 10, 10});
    auto mirror = std::make_unique<doc::CopyFeature>();
    mirror->sourceBody = a;
    mirror->mirror = true;
    mirror->planeOrigin = {0, 0, 0};
    mirror->planeNormal = {1, 0, 0};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Body 2", std::move(mirror));
    const Uuid b = create->bodyId();
    ASSERT_TRUE(m.push(std::move(create)).ok());
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetBodyVisibilityCommand>(a, false)).ok());

    auto command = std::make_unique<cmd::DuplicateBodyCommand>(b);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());
    EXPECT_EQ(m.document.bodies().size(), 3u) << "the hidden source is not copied";
    EXPECT_EQ(static_cast<const doc::CopyFeature&>(*m.body(copy).features()[0]).sourceBody, a);
    EXPECT_NEAR(m.bounds(copy).min.x, -15.0, 1e-6);

    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(a).features()[0]->id(), "width", 20.0)).ok());
    EXPECT_NEAR(m.bounds(b).min.x, -25.0, 1e-6);
    EXPECT_NEAR(m.bounds(copy).min.x, -25.0, 1e-6) << "the duplicate follows the shared source";
}

// A Combine tool the user showed again is still consumed by the body: the
// duplicate takes its own (hidden) copy, so editing the tool changes only the
// source.
TEST(Duplicate, ShownToolBodyIsStillCopied)
{
    Model m;
    const Uuid a = m.box("Body 1", {0, 0, 0}, {20, 20, 20});
    const Uuid b = m.box("Body 2", {5, 5, 15}, {10, 10, 10});
    auto combine = std::make_unique<doc::CombineFeature>();
    combine->toolBody = b;
    combine->mode = doc::CombineMode::Subtract;
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(a, std::move(combine))).ok());
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetBodyVisibilityCommand>(b, false)).ok());
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetBodyVisibilityCommand>(b, true)).ok()); // shown to look at it

    auto command = std::make_unique<cmd::DuplicateBodyCommand>(a);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(m.push(std::move(command)).ok());
    ASSERT_EQ(m.document.bodies().size(), 4u);
    const auto& copyCombine = static_cast<const doc::CombineFeature&>(*m.body(copy).features()[1]);
    ASSERT_NE(copyCombine.toolBody, b);
    ASSERT_NE(m.document.body(copyCombine.toolBody), nullptr);
    EXPECT_FALSE(m.document.body(copyCombine.toolBody)->isVisible()) << "the copied tool is consumed: hidden";

    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(b).features()[0]->id(), "height", 3.0)).ok());
    EXPECT_NEAR(m.volume(a), 8000.0 - 300.0, 1e-6);
    EXPECT_NEAR(m.volume(copy), 7500.0, 1e-6);
}

// A copy that took its source in with a union: the source is its Combine
// tool, so the duplicate copies it too and is independent of it.
TEST(Duplicate, UnionOfACopyWithItsSourceIsCopiedWhole)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    auto mirror = std::make_unique<doc::CopyFeature>();
    mirror->sourceBody = a;
    mirror->mirror = true;
    mirror->planeOrigin = {10, 0, 0};
    mirror->planeNormal = {1, 0, 0};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>("Body 2", std::move(mirror)), h.document).ok());
    h.controller.documentChanged();
    const Uuid b = h.document.bodies().back()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("union").ok()); // b keeps the result, a is its hidden tool

    auto command = std::make_unique<cmd::DuplicateBodyCommand>(b);
    const Uuid copy = command->copyId();
    ASSERT_TRUE(h.stack.push(std::move(command), h.document).ok());
    ASSERT_EQ(h.document.bodies().size(), 4u);
    EXPECT_NEAR(geom::volume(h.document.body(copy)->shape()), 2000.0, 1e-6);
    const auto& base = static_cast<const doc::CopyFeature&>(*h.document.body(copy)->features()[0]);
    EXPECT_NE(base.sourceBody, a) << "mirrors the copied source";

    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::SetParameterCommand>(h.document.body(a)->features()[0]->id(), "width", 5.0),
                             h.document)
                    .ok());
    EXPECT_NEAR(geom::volume(h.document.body(b)->shape()), 1000.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.document.body(copy)->shape()), 2000.0, 1e-6);
    EXPECT_FALSE(h.document.body(copy)->hasFailures());
}

// On touch the corner zones (36 px) would cover all of a short edge; they are
// limited to a quarter of the edge on screen, so a tap on its middle still
// makes it the axis, and a tap on its end still moves the pivot there.
TEST(RotateAbout, ShortEdgeMiddlePicksTheAxisOnTouch)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {20, 10, 2});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("rotate").ok());
    const double onScreen = (h.screen({20, 0, 2}) - h.screen({20, 0, 0})).length();
    ASSERT_LT(onScreen, 4 * InputProfile::forDevice(PointerDevice::Touch).pickTolerance) << "a short edge";
    ASSERT_GT(onScreen, 8.0);

    h.clickAt(h.screen({20, 0, 1}), PointerDevice::Touch);
    const auto* rotate = dynamic_cast<const RotateOperation*>(h.controller.operation());
    ASSERT_NE(rotate, nullptr);
    ASSERT_TRUE(rotate->axis().has_value()) << "the middle of the edge picks the axis";
    EXPECT_NEAR(rotate->axis()->z, 1.0, 1e-12);
    EXPECT_EQ(rotate->ringCount(), 1);
    EXPECT_NEAR((rotate->center() - Vec3{20, 0, 1}).length(), 0.0, 1e-9);

    ASSERT_TRUE(h.controller.triggerAction("pivotCenter").ok());
    h.clickAt(h.screen({20, 0, 2}), PointerDevice::Touch);
    rotate = dynamic_cast<const RotateOperation*>(h.controller.operation());
    ASSERT_NE(rotate, nullptr);
    EXPECT_FALSE(rotate->axis().has_value());
    EXPECT_NEAR((rotate->center() - Vec3{20, 0, 2}).length(), 0.0, 1e-9) << "its end picks the corner";
    EXPECT_EQ(rotate->ringCount(), 3);
}

// The rings cross the body; a tap on a ring where it crosses a shaft (or a
// hole) turns about the shaft's axis, as a tap on it elsewhere does (on touch
// the rings' tap zones are wide).
TEST(RotateAbout, RingOverAShaftPicksItsAxis)
{
    Model m;
    sketch::Sketch s(Uuid::generate(), sketch::Plane::xy());
    s.setName("Sketch 1");
    s.addCircle(s.addPoint({30, 0}), 5);
    const Uuid sketchId = s.id();
    ASSERT_TRUE(m.push(std::make_unique<cmd::CreateSketchCommand>(std::move(s))).ok());
    const Uuid disk = m.extrude(sketchId, 10);
    InteractionController controller(m.document, m.stack);
    controller.setViewportSize({1200, 800});
    controller.fitAll(false);
    ASSERT_TRUE(controller.selectBody(disk, false).ok());
    ASSERT_TRUE(controller.runTool("rotate").ok());
    const Operation* op = controller.operation();
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->ringCount(), 3);

    // A point on a ring that picks the round side.
    const InputProfile touch = InputProfile::forDevice(PointerDevice::Touch);
    std::optional<Vec2> spot;
    for (int i = 0; i < 3 && !spot; ++i)
        for (int k = 0; k < 72 && !spot; ++k) {
            const Vec2 p = controller.camera().project(op->ring(i).pointAt(controller.camera(), 2 * kPi * k / 72));
            const sel::PickResult hit = controller.pickAt(p, touch);
            const auto face = hit.kind == sel::PickKind::Face ? geom::faceInfo(m.body(disk).shape(), hit.index) : std::nullopt;
            if (face && face->hasAxis())
                spot = p;
        }
    ASSERT_TRUE(spot.has_value()) << "a ring crosses the shaft's side";
    controller.pointerPress(Harness::at(*spot, PointerDevice::Touch));
    controller.pointerRelease(Harness::at(*spot, PointerDevice::Touch));
    const auto* rotate = dynamic_cast<const RotateOperation*>(controller.operation());
    ASSERT_NE(rotate, nullptr);
    ASSERT_TRUE(rotate->axis().has_value()) << "the tap turned about the shaft, not activated the ring";
    EXPECT_NEAR(rotate->axis()->z, 1.0, 1e-9);
    EXPECT_NEAR(rotate->center().x, 30.0, 1e-6);
    EXPECT_NEAR(rotate->center().y, 0.0, 1e-6);
    EXPECT_EQ(rotate->ringCount(), 1);
}

// Model panel rows: a click replaces the selection, Shift-click toggles, and
// a tap in the touch layout adds (never takes out: tapping a selected body's
// row again, e.g. to fold the row, keeps it selected and its Move running).
TEST(SelectBody, TapOnARowAddsAndNeverTakesOut)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    const Uuid b = h.addBox("Body 2", {30, 0, 0}, {10, 10, 10});
    using Pick = InteractionController::BodyPick;
    const auto selected = [&] {
        std::vector<Uuid> ids;
        for (const auto& item : h.controller.selection().items())
            ids.push_back(item.bodyId);
        return ids;
    };

    ASSERT_TRUE(h.controller.selectBody(a, Pick::Add).ok());
    EXPECT_EQ(selected(), (std::vector<Uuid>{a}));
    ASSERT_NE(h.controller.operation(), nullptr);
    ASSERT_EQ(h.controller.operation()->title(), "Move");
    const Operation* move = h.controller.operation();
    ASSERT_TRUE(h.controller.selectBody(a, Pick::Add).ok()); // the row folds
    EXPECT_EQ(selected(), (std::vector<Uuid>{a})) << "still selected";
    EXPECT_EQ(h.controller.operation(), move) << "the same Move, not rebuilt";

    ASSERT_TRUE(h.controller.selectBody(b, Pick::Add).ok());
    EXPECT_EQ(selected(), (std::vector<Uuid>{a, b}));
    ASSERT_TRUE(h.controller.selectBody(a, Pick::Add).ok());
    EXPECT_EQ(selected(), (std::vector<Uuid>{a, b})) << "tapping either row again keeps both";
    ASSERT_TRUE(h.hasAction("union")) << "two bodies to combine";

    // Shift-click still toggles; a plain click replaces.
    ASSERT_TRUE(h.controller.selectBody(a, Pick::Toggle).ok());
    EXPECT_EQ(selected(), (std::vector<Uuid>{b}));
    ASSERT_TRUE(h.controller.selectBody(a, Pick::Replace).ok());
    EXPECT_EQ(selected(), (std::vector<Uuid>{a}));

    // A face selected: the tap selects the body instead (as a click does).
    (void)h.controller.keyPress(Key::Escape);
    ASSERT_TRUE(h.controller.selection().empty());
    h.controller.fitAll(false);
    h.clickAt(h.controller.camera().project({5, 5, 10}));
    ASSERT_TRUE(h.controller.selection().allOfKind(sel::SelectionKind::Face));
    ASSERT_TRUE(h.controller.selectBody(a, Pick::Add).ok());
    EXPECT_EQ(selected(), (std::vector<Uuid>{a}));
    EXPECT_TRUE(h.controller.selection().allOfKind(sel::SelectionKind::Body));
}
