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

    static PointerEvent at(Vec2 p)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        return e;
    }
    void clickAt(Vec2 p)
    {
        controller.pointerPress(at(p));
        controller.pointerRelease(at(p));
    }
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

// A plate cut in two by a slot becomes two bodies; widening the slot upstream
// updates both; undo, redo and a saved file keep it.
TEST(Split, PlateCutInTwoBecomesTwoBodies)
{
    Model m;
    const SlottedPlate p = slottedPlate(m);
    ASSERT_EQ(m.body(p.plate).shape().solidCount(), 2);
    EXPECT_NEAR(m.volume(p.plate), 2 * 2800.0, 1e-6);

    auto split = cmd::makeSplitBodyCommand(m.document, p.plate);
    ASSERT_TRUE(split.ok()) << split.developerMessage();
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
    EXPECT_EQ(m.body(piece).features().front()->kind(), doc::FeatureKind::SplitPiece);

    // A wider slot (the left edge stays at 28): the right piece shrinks.
    m.setSketchDimension(p.slotSketch, p.slotWidth, 10);
    EXPECT_FALSE(m.body(p.plate).hasFailures());
    EXPECT_FALSE(m.body(piece).hasFailures());
    EXPECT_NEAR(m.volume(p.plate), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(piece), 22 * 20 * 5.0, 1e-6);
    EXPECT_NEAR(m.bounds(piece).min.x, 38.0, 1e-6);

    // Undo the edit, then the split; redo both.
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_NEAR(m.volume(piece), 2800.0, 1e-6);
    ASSERT_TRUE(m.stack.undo(m.document));
    EXPECT_EQ(m.document.bodies().size(), 1u);
    EXPECT_EQ(m.body(p.plate).shape().solidCount(), 2);
    ASSERT_TRUE(m.stack.redo(m.document).ok());
    ASSERT_EQ(m.document.bodies().size(), 2u);
    EXPECT_NEAR(m.volume(m.document.bodies()[1]->id()), 2800.0, 1e-6);
    ASSERT_TRUE(m.stack.redo(m.document).ok());
    EXPECT_NEAR(m.volume(m.document.bodies()[1]->id()), 2200.0, 1e-6);

    // Save and reopen: still two linked bodies.
    const auto path = tempPath("split.openshape");
    ASSERT_TRUE(io::saveProject(m.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_EQ(d.bodies().size(), 2u);
    EXPECT_NEAR(geom::volume(d.bodies()[0]->shape()), 2800.0, 1e-6);
    EXPECT_NEAR(geom::volume(d.bodies()[1]->shape()), 2200.0, 1e-6);
    EXPECT_FALSE(d.bodies()[1]->hasFailures());
    std::filesystem::remove(path);
}

// When the body is whole again (the cut is suppressed), the body keeps all of
// it and the split-off piece says why it cannot be built.
TEST(Split, PieceFailsClearlyWhenItNoLongerExists)
{
    Model m;
    const SlottedPlate p = slottedPlate(m);
    auto split = cmd::makeSplitBodyCommand(m.document, p.plate);
    ASSERT_TRUE(split.ok());
    ASSERT_TRUE(m.push(std::move(split.value())).ok());
    const Uuid piece = m.document.bodies()[1]->id();
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

    // A body in one piece cannot be split.
    const Uuid single = m.box("Body 9", {100, 0, 0}, {5, 5, 5});
    EXPECT_FALSE(cmd::makeSplitBodyCommand(m.document, single).ok());
}

// Three pieces: the body keeps the largest, two new bodies; a new cut later
// adds a piece, which stays in the body rather than vanishing.
TEST(Split, ThreePiecesAndAPieceThatAppearsLater)
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
    ASSERT_EQ(m.document.bodies().size(), 5u);
    EXPECT_NEAR(m.volume(bar), 4800.0, 1e-6) << "keeps the largest piece";
    EXPECT_NEAR(m.volume(m.document.bodies()[3]->id()), 2800.0, 1e-6);
    EXPECT_NEAR(m.volume(m.document.bodies()[4]->id()), 2000.0, 1e-6);
    EXPECT_EQ(m.document.bodies()[3]->name(), "Body 2");
    EXPECT_EQ(m.document.bodies()[4]->name(), "Body 3");

    // A wider cut A (its tool is upstream of the bar): the pieces follow
    // (tool -> bar -> pieces).
    ASSERT_TRUE(m.push(std::make_unique<cmd::SetParameterCommand>(m.body(cutA).features()[0]->id(), "width", 4.0)).ok());
    EXPECT_NEAR(m.volume(bar), 4600.0, 1e-6);
    EXPECT_NEAR(m.volume(m.document.bodies()[4]->id()), 2000.0, 1e-6);

    // Another cut before the split step makes a fourth piece: it stays in the body.
    const Uuid cutC = m.box("Cut C", {40, -1, -1}, {2, 12, 12});
    auto combine = std::make_unique<doc::CombineFeature>();
    combine->toolBody = cutC;
    combine->mode = doc::CombineMode::Subtract;
    ASSERT_TRUE(m.push(std::make_unique<cmd::AddFeatureCommand>(bar, std::move(combine), 3)).ok());
    EXPECT_FALSE(m.body(bar).hasFailures());
    EXPECT_EQ(m.body(bar).shape().solidCount(), 2);
    EXPECT_NEAR(m.volume(bar), 4600.0 - 200.0, 1e-6);
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
    ASSERT_TRUE(h.controller.triggerAction("split").ok());
    ASSERT_EQ(h.document.bodies().size(), 3u);
    EXPECT_EQ(h.document.body(a)->shape().solidCount(), 1);
    EXPECT_NEAR(geom::volume(h.document.bodies().back()->shape()), 1000.0, 1e-6);
    EXPECT_FALSE(h.hasAction("split"));
    bool detail = false, stillFlagged = false;
    for (const auto& row : h.controller.historyRows()) {
        detail = detail || (row.name == "Piece" && row.detail == "Piece 2 of Body 1");
        stillFlagged = stillFlagged || row.status == HistoryRow::Status::Warning || row.canSplit;
    }
    EXPECT_TRUE(detail);
    EXPECT_FALSE(stillFlagged) << "the split dealt with the pieces the union left";
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 2u);
}

// Duplicating a split-off piece: its source body is visible, so it stays
// shared, and the copy follows the source's upstream edits like the piece.
TEST(Split, DuplicatedPieceKeepsFollowingItsSource)
{
    Model m;
    const SlottedPlate p = slottedPlate(m);
    auto split = cmd::makeSplitBodyCommand(m.document, p.plate);
    ASSERT_TRUE(split.ok());
    ASSERT_TRUE(m.push(std::move(split.value())).ok());
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

// A step that leaves the body in pieces says how to split it.
TEST(Split, MirrorThatLeavesPiecesSuggestsTheSplit)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {5, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("Split into bodies"), std::string::npos) << h.messages.back();
}
