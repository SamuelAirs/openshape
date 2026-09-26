// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bodies and copies: duplicate, split into bodies, mirror/pattern as separate
// bodies, rotate about a picked edge or point.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"

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
