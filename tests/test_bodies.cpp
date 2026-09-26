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

// ---- Mirror / Pattern as separate bodies ---------------------------------------------

namespace {
const HistoryRow* rowNamed(const std::vector<HistoryRow>& rows, const std::string& name)
{
    for (const auto& r : rows)
        if (r.name == name)
            return &r;
    return nullptr;
}
} // namespace

// "Separate bodies": the mirror image is a body of its own that follows the
// original when it changes.
TEST(Copies, MirrorAsSeparateBody)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {5, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.hasAction("separate"));
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok()); // across YZ
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    const auto* mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->separate());
    EXPECT_TRUE(mirror->canCommit()) << mirror->error();
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_EQ(h.stack.undoLabel(), "Mirror");
    ASSERT_EQ(h.document.bodies().size(), 2u);
    const doc::Body& source = *h.document.body(a);
    const doc::Body& image = *h.document.bodies().back();
    EXPECT_EQ(source.features().size(), 1u) << "the original gets no step";
    EXPECT_NEAR(geom::volume(source.shape()), 1000.0, 1e-6);
    EXPECT_NEAR(geom::volume(image.shape()), 1000.0, 1e-6);
    EXPECT_EQ(image.name(), "Body 2");
    EXPECT_NEAR(geom::boundingBox(image.shape()).min.x, -15.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(image.shape()).max.x, -5.0, 1e-6);
    const auto rows = h.controller.historyRows();
    const HistoryRow* copyRow = rowNamed(rows, "Mirror copy");
    ASSERT_NE(copyRow, nullptr);
    EXPECT_EQ(copyRow->detail, "Of Body 1 \xC2\xB7 Across YZ");

    // The original gets wider: the image follows.
    ASSERT_TRUE(h.controller.setFeatureParameter(source.features()[0]->id(), "width", "20").ok());
    EXPECT_NEAR(geom::boundingBox(image.shape()).min.x, -25.0, 1e-6);
    EXPECT_NEAR(geom::volume(image.shape()), 2000.0, 1e-6);

    EXPECT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
}

TEST(Copies, PatternAsSeparateBodies)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0)..(10,10,20)
    const Uuid a = h.document.bodies().front()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern->separate());
    EXPECT_TRUE(pattern->hasPreview());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.stack.undoLabel(), "Pattern");
    ASSERT_EQ(h.document.bodies().size(), 3u);
    EXPECT_EQ(h.document.body(a)->features().size(), 1u);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto bb = geom::boundingBox(h.document.bodies()[i]->shape());
        EXPECT_NEAR(bb.min.x, -10.0 + 25.0 * double(i), 1e-6) << i;
        EXPECT_NEAR(geom::volume(h.document.bodies()[i]->shape()), 8000.0, 1e-6) << i;
    }
    // The source stays selected (with plain Move arrows).
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].bodyId, a);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Move");

    // The original gets taller: every copy follows.
    ASSERT_TRUE(h.controller.setFeatureParameter(h.document.body(a)->features()[0]->id(), "height", "30").ok());
    for (std::size_t i = 1; i < 3; ++i)
        EXPECT_NEAR(geom::boundingBox(h.document.bodies()[i]->shape()).size().z, 30.0, 1e-6) << i;

    // Save and reopen.
    const auto path = tempPath("copies.openshape");
    ASSERT_TRUE(io::saveProject(h.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    ASSERT_EQ(loaded.value()->bodies().size(), 3u);
    EXPECT_NEAR(geom::boundingBox(loaded.value()->bodies()[2]->shape()).min.x, 40.0, 1e-6);
    EXPECT_NEAR(geom::volume(loaded.value()->bodies()[2]->shape()), 12000.0, 1e-6);
    std::filesystem::remove(path);

    EXPECT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
}

// A circular pattern as separate bodies: turned copies, stored with their
// rotation; a file round trip keeps them.
TEST(Copies, CircularPatternAsSeparateBodies)
{
    Harness h;
    const Uuid bar = h.addBox("Bar", {5, -1, 0}, {10, 2, 2});
    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("layout:circular").ok());
    ASSERT_TRUE(h.controller.triggerAction("fewer").ok());
    ASSERT_TRUE(h.controller.triggerAction("fewer").ok()); // 4 in total, 90 degrees apart
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 4u);
    const auto* copy = dynamic_cast<const doc::CopyFeature*>(h.document.bodies()[2]->features()[0].get());
    ASSERT_NE(copy, nullptr);
    EXPECT_NEAR(std::abs(copy->motion.angle), kPi, 1e-9);
    for (std::size_t i = 1; i < 4; ++i)
        EXPECT_NEAR(geom::volume(h.document.bodies()[i]->shape()), 40.0, 1e-6);
    const auto path = tempPath("circular.openshape");
    ASSERT_TRUE(io::saveProject(h.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    const auto* reread = dynamic_cast<const doc::CopyFeature*>(loaded.value()->bodies()[2]->features()[0].get());
    ASSERT_NE(reread, nullptr);
    EXPECT_NEAR(reread->motion.angle, copy->motion.angle, 1e-12);
    EXPECT_EQ(reread->sourceBody, bar);
    const auto a = geom::boundingBox(h.document.bodies()[2]->shape());
    const auto b = geom::boundingBox(loaded.value()->bodies()[2]->shape());
    EXPECT_NEAR((a.min - b.min).length() + (a.max - b.max).length(), 0.0, 1e-9);
    std::filesystem::remove(path);
}

// A copy is built from its source, so the source cannot take it as a tool;
// a union of the two still works (the copy keeps the result).
TEST(Copies, UnionOfACopyWithItsSource)
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

TEST(Copies, TooManySeparateBodiesAreRefused)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {1, 1, 1});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    auto* pattern = const_cast<PatternOperation*>(dynamic_cast<const PatternOperation*>(h.controller.operation()));
    ASSERT_NE(pattern, nullptr);
    pattern->setCount(102, h.document);
    EXPECT_FALSE(pattern->canCommit());
    EXPECT_NE(pattern->error().find("up to 100"), std::string::npos) << pattern->error();
    pattern->setCount(101, h.document);
    EXPECT_TRUE(pattern->canCommit()) << pattern->error();
}

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
}

// ---- Rotate about a picked edge or point ------------------------------------------

namespace {
void expectBox(const geom::BoundingBox& bb, Vec3 min, Vec3 max)
{
    EXPECT_NEAR(bb.min.x, min.x, 1e-6);
    EXPECT_NEAR(bb.min.y, min.y, 1e-6);
    EXPECT_NEAR(bb.min.z, min.z, 1e-6);
    EXPECT_NEAR(bb.max.x, max.x, 1e-6);
    EXPECT_NEAR(bb.max.y, max.y, 1e-6);
    EXPECT_NEAR(bb.max.z, max.z, 1e-6);
}
} // namespace

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

// Deleting the body a piece was split off would break the piece: the body is
// hidden instead (one undo step) and the piece stays exactly as it was.
// Deleting both at once deletes both.
TEST(Delete, SplitParentIsHiddenNotDeleted)
{
    Harness h;
    const Uuid plate = h.addBox("Body 1", {0, 0, 0}, {60, 20, 5});
    const Uuid slot = h.addBox("Slot", {28, -5, -5}, {4, 30, 15});
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.selectBody(slot, true).ok());
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    ASSERT_TRUE(h.controller.selectBody(plate, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("split").ok());
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

// Of a body and one of its two split-off pieces, the piece is deleted and the
// body hidden (the other piece is built from it); the message names only the
// body that stays.
TEST(Delete, ParentAndOneOfTwoPieces)
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
    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("split").ok());
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

// The same for the source of separate copies (Pattern -> Separate bodies).
TEST(Delete, CopySourceIsHiddenNotDeleted)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    const Uuid a = h.document.bodies().front()->id();
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
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
