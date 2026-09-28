// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Loft through the interaction layer: selecting profiles on different planes
// (Shift-click, taps), the Loft action and palette tool, its options, the
// automatic join / new body choice, the Model panel's Smooth / Straight, and
// previews on the worker.
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Datum.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchWording.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace os;
using namespace os::interact;

namespace {

double frustum(double a1, double a2, double h)
{
    return h / 3 * (a1 + a2 + std::sqrt(a1 * a2));
}

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;

    explicit Harness(bool async = false)
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
        if (async)
            controller.enableAsyncPreviews([] {});
    }
    static PointerEvent at(Vec2 p, PointerDevice device = PointerDevice::Mouse, bool shift = false)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        e.device = device;
        e.modifiers.shift = shift;
        return e;
    }
    void click(const Vec3& p, bool shift = false)
    {
        const auto e = at(controller.camera().project(p), PointerDevice::Mouse, shift);
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    void tap(const Vec3& p)
    {
        const auto e = at(controller.camera().project(p), PointerDevice::Touch);
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    void tapScreen(Vec2 p)
    {
        const auto e = at(p, PointerDevice::Touch);
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    // A construction plane `height` above the ground.
    Uuid plane(double height)
    {
        doc::Datum datum;
        datum.method = doc::DatumMethod::PlaneOffset;
        datum.originIndex = 2;
        datum.distance = height;
        const Uuid id = datum.id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddDatumCommand>(std::move(datum)), document).ok());
        return id;
    }
    // A sketch with a square of `side` around `center` (sketch coordinates),
    // on the ground or on a construction plane.
    Uuid square(double side, std::optional<Uuid> onPlane = std::nullopt, Vec2 center = {})
    {
        sketch::Sketch sk = blank(onPlane);
        sketch::addRectangle(sk, center + Vec2{-side / 2, -side / 2}, center + Vec2{side / 2, side / 2});
        return add(std::move(sk));
    }
    Uuid circle(double radius, std::optional<Uuid> onPlane = std::nullopt)
    {
        sketch::Sketch sk = blank(onPlane);
        sk.addCircle(sk.addPoint({0, 0}), radius);
        return add(std::move(sk));
    }
    sketch::Sketch blank(std::optional<Uuid> onPlane) const
    {
        sketch::Sketch sk(Uuid::generate(),
                          onPlane ? doc::sketchPlaneOn(document.datum(*onPlane)->geometry()) : sketch::Plane::xy());
        if (onPlane)
            sk.setDatumPlane(*onPlane);
        return sk;
    }
    Uuid add(sketch::Sketch sk)
    {
        const Uuid id = sk.id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::CreateSketchCommand>(std::move(sk)), document).ok());
        controller.documentChanged();
        return id;
    }
    void view()
    {
        controller.setStandardView(StandardView::Isometric, false);
        controller.fitAll(false);
    }
    // Whether a press at `p` lands on an arrow of the armed operation (a
    // finger's reach from it).
    bool underArrow(const Vec3& p) const
    {
        const Operation* op = controller.operation();
        const Vec2 s = controller.camera().project(p);
        for (int i = 0; op && i < op->handleCount(); ++i)
            if (op->handle(i).hitTest(controller.camera(), s, op->handleOffset(i), 28).has_value())
                return true;
        return false;
    }
    const LoftOperation* loft() const { return dynamic_cast<const LoftOperation*>(controller.operation()); }
    bool offers(const std::string& id) const
    {
        const auto actions = controller.contextActions();
        return std::any_of(actions.begin(), actions.end(), [&](const ContextAction& a) { return a.id == id; });
    }
    bool active(const std::string& id) const
    {
        const auto actions = controller.contextActions();
        return std::any_of(actions.begin(), actions.end(), [&](const ContextAction& a) { return a.id == id && a.active; });
    }
    double volume(std::size_t body) const { return geom::volume(document.bodies()[body]->shape()); }
};

} // namespace

TEST(LoftInteraction, ShiftClickTheSecondProfileThenLoft)
{
    Harness h;
    const Uuid high = h.plane(30);
    h.square(20);
    h.square(10, high);
    h.view();

    // Click the lower square, Shift-click the upper one: two profiles, no
    // extrusion (they are not one sketch's), Loft offered.
    h.click({7, 7, 0});
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.operation()->title(), "Extrude");
    // The lower square's extrusion arrow points up through the upper square:
    // a click on it there (not a drag) picks the profile under it.
    EXPECT_TRUE(h.underArrow({3, 3, 30}));
    h.click({3, 3, 30}, true);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_TRUE(h.offers("loft"));
    EXPECT_EQ(h.controller.selectionSummary(), "2 profiles \xC2\xB7 500.00 mm\xC2\xB2");

    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_EQ(h.loft()->title(), "Loft");
    EXPECT_TRUE(h.loft()->hasPreview());
    EXPECT_TRUE(h.loft()->error().empty()) << h.loft()->error();
    EXPECT_TRUE(h.active("loft"));
    EXPECT_TRUE(h.active("loft:smooth"));
    EXPECT_FALSE(h.active("loft:straight"));
    EXPECT_FALSE(h.offers("mode:join")) << "no sketch on a body: always a new body";
    EXPECT_TRUE(h.offers("apply"));
    EXPECT_FALSE(h.controller.valueLabelPosition().has_value()) << "no value: the actions stay in the bar";
    ASSERT_EQ(h.loft()->sections().size(), 2u);
    EXPECT_NEAR(h.document.sketch(h.loft()->sections()[1].sketchId)->plane().origin.z, 30, 1e-9) << "in selection order";

    ASSERT_TRUE(h.controller.triggerAction("loft:straight").ok());
    EXPECT_TRUE(h.active("loft:straight"));
    EXPECT_TRUE(h.loft()->ruled());
    EXPECT_TRUE(h.loft()->hasPreview());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(h.volume(0), 7000.0, 1e-6);
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.controller.operation(), nullptr);
    ASSERT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.document.bodies().empty());
    ASSERT_TRUE(h.controller.redo());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(h.volume(0), 7000.0, 1e-6);

    // The Model panel row: "2 profiles · Straight · New body", Smooth /
    // Straight as a choice.
    const Uuid step = h.document.bodies()[0]->features()[0]->id();
    bool found = false;
    for (const auto& row : h.controller.historyRows()) {
        if (row.id != step)
            continue;
        found = true;
        EXPECT_EQ(row.name, "Loft");
        EXPECT_EQ(row.detail, "2 profiles \xC2\xB7 Straight \xC2\xB7 New body");
        ASSERT_EQ(row.parameters.size(), 1u);
        EXPECT_EQ(row.parameters[0].key, "sections");
        EXPECT_EQ(row.parameters[0].valueText, "Straight");
        EXPECT_EQ(row.parameters[0].choices, (std::vector<std::string>{"Smooth", "Straight"}));
    }
    EXPECT_TRUE(found);
    ASSERT_TRUE(h.controller.setFeatureParameter(step, "sections", "Smooth").ok());
    EXPECT_NEAR(h.volume(0), 7000.0, 1e-6); // two profiles: the same solid
    EXPECT_EQ(static_cast<const doc::LoftFeature&>(*h.document.bodies()[0]->features()[0]).ruled, false);
    EXPECT_FALSE(h.controller.setFeatureParameter(step, "sections", "Bumpy").ok());
}

TEST(LoftInteraction, TapsAddSectionsInOrder)
{
    Harness h;
    const Uuid mid = h.plane(15), top = h.plane(30);
    h.square(10);
    h.square(30, mid);
    h.square(10, top);
    // The planes' outlines are targets too (a finger's reach): hidden here,
    // only the squares are.
    ASSERT_TRUE(h.controller.setDatumVisible(mid, false).ok());
    ASSERT_TRUE(h.controller.setDatumVisible(top, false).ok());
    h.view();

    // On touch every tap adds: the ground, the middle, the top square.
    h.tap({4, 4, 0});
    ASSERT_EQ(h.controller.selection().size(), 1u);
    h.tap({12, 12, 15});
    ASSERT_EQ(h.controller.selection().size(), 2u);
    EXPECT_TRUE(h.controller.selection().allOfKind(sel::SelectionKind::SketchProfile));
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    ASSERT_EQ(h.loft()->sections().size(), 2u);
    ASSERT_TRUE(h.controller.triggerAction("loft:straight").ok());
    // With the loft armed, a tap on another profile adds it (not Apply).
    h.tap({3, 3, 30});
    ASSERT_NE(h.loft(), nullptr) << "the tap added a section instead of applying";
    EXPECT_EQ(h.loft()->sections().size(), 3u);
    EXPECT_TRUE(h.loft()->ruled()) << "the choice stays while sections are added";
    EXPECT_TRUE(h.document.bodies().empty());
    // Tapped again, it comes out; and back in.
    h.tap({3, 3, 30});
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_EQ(h.loft()->sections().size(), 2u);
    h.tap({3, 3, 30});
    ASSERT_EQ(h.loft()->sections().size(), 3u);
    EXPECT_TRUE(h.loft()->hasPreview());
    // A tap on empty space applies it.
    h.tapScreen({15, 785});
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(h.volume(0), 2 * frustum(100, 900, 15), 1e-6);
    const auto box = geom::boundingBox(h.document.bodies()[0]->shape());
    EXPECT_NEAR(box.size().x, 30, 1e-6);
    EXPECT_NEAR(box.size().z, 30, 1e-6);
}

// A small circle on a construction plane, seen from afar: the plane's
// outline is within a finger's reach (18 px) of the whole circle. A tap
// inside the circle still takes the circle (the outline only within a
// mouse's reach), so it can be a loft's next section on a phone.
TEST(LoftInteraction, TapInsideASmallProfileOnAPlaneTakesTheProfile)
{
    Harness h;
    const Uuid top = h.plane(30);
    h.square(20);
    const Uuid circle = h.circle(5, top);
    h.view();
    // Zoomed out until the plane's outline (20 mm from its middle: the
    // model is small) is closer than 15 px to the circle's middle.
    auto& camera = h.controller.camera();
    auto far = [&] {
        const Vec2 m = camera.project({0, 0, 30});
        return std::max((camera.project({20, 0, 30}) - m).length(), (camera.project({0, 20, 30}) - m).length()) > 14;
    };
    for (int i = 0; i < 200 && far(); ++i)
        h.controller.wheel(camera.project({0, 0, 30}), -1);
    ASSERT_FALSE(far());
    const Vec2 middle = camera.project({0, 0, 30});
    const sel::PickResult hit = h.controller.pickAt(middle, InputProfile::forDevice(PointerDevice::Touch));
    EXPECT_EQ(hit.kind, sel::PickKind::Profile);
    EXPECT_EQ(hit.bodyId, circle);
    // A mouse never reached the outline from there, and still does not.
    EXPECT_EQ(h.controller.pickAt(middle, InputProfile::forDevice(PointerDevice::Mouse)).kind, sel::PickKind::Profile);
    // Beside the circle the plane is still a finger's target.
    const double radius = (camera.project({5, 0, 30}) - middle).length();
    const sel::PickResult beside = h.controller.pickAt(middle + Vec2{radius + 4, 0}, InputProfile::forDevice(PointerDevice::Touch));
    EXPECT_EQ(beside.kind, sel::PickKind::Datum);
    EXPECT_EQ(beside.bodyId, top);
    // And the tap adds the circle to the ground square: a loft.
    h.view();
    h.tap({3, 3, 0});
    ASSERT_EQ(h.controller.selection().size(), 1u);
    for (int i = 0; i < 200 && far(); ++i)
        h.controller.wheel(camera.project({0, 0, 30}), -1);
    h.tap({0, 0, 30});
    EXPECT_EQ(h.controller.selection().size(), 2u);
    EXPECT_TRUE(h.offers("loft"));
}

TEST(LoftInteraction, ProfilesInOnePlaneAreNoLoft)
{
    Harness h;
    h.square(10);
    sketch::Sketch beside = h.blank(std::nullopt);
    sketch::addRectangle(beside, {20, -5}, {30, 5});
    h.add(std::move(beside));
    h.view();
    // Shift-click a profile of another sketch in the same plane: it replaces
    // the selection (they could neither extrude together nor loft).
    h.click({4, 4, 0});
    h.click({25, 0, 0}, true);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_FALSE(h.offers("loft"));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Extrude");

    // Two profiles of one sketch through the palette: the loft explains.
    sketch::Sketch two = h.blank(std::nullopt);
    sketch::addRectangle(two, {-20, 20}, {-10, 30});
    sketch::addRectangle(two, {0, 20}, {10, 30});
    h.add(std::move(two));
    h.view();
    h.click({-15, 25, 0});
    h.click({5, 25, 0}, true);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    ASSERT_TRUE(h.controller.runTool("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_FALSE(h.loft()->canCommit());
    EXPECT_NE(h.loft()->error().find("same plane"), std::string::npos) << h.loft()->error();
    EXPECT_TRUE(h.offers("extrude")) << "one sketch's profiles can still extrude";
    EXPECT_FALSE(h.offers("apply"));
    EXPECT_FALSE(h.controller.commitOperation().ok());
    EXPECT_TRUE(h.document.bodies().empty());
    ASSERT_TRUE(h.controller.triggerAction("extrude").ok());
    EXPECT_EQ(h.controller.operation()->title(), "Extrude");
}

TEST(LoftInteraction, PaletteSaysWhatToSelect)
{
    Harness h;
    h.controller.setTouchLayout(true);
    EXPECT_FALSE(h.controller.runTool("loft").ok());
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("Loft"), std::string::npos);
    EXPECT_FALSE(mentionsMouseOrKeyboard(h.messages.back())) << h.messages.back();
    h.controller.setTouchLayout(false);
    EXPECT_FALSE(h.controller.runTool("loft").ok());
    EXPECT_NE(h.messages.back().find("Shift-click"), std::string::npos) << h.messages.back();
    // One profile: not yet.
    h.square(10);
    h.view();
    h.click({4, 4, 0});
    EXPECT_FALSE(h.controller.runTool("loft").ok());
    EXPECT_EQ(h.controller.operation()->title(), "Extrude");
    EXPECT_FALSE(h.controller.triggerAction("loft").ok());
}

TEST(LoftInteraction, FromABodysFaceJoinsItOrMakesANewBody)
{
    Harness h;
    // A 20 x 20 x 10 block centered on the origin, a sketch on its top face.
    auto block = std::make_unique<doc::BoxFeature>();
    block->origin = {-10, -10, 0};
    block->size = {20, 20, 10};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid body = create->bodyId();
    ASSERT_TRUE(h.stack.push(std::move(create), h.document).ok());
    sketch::Sketch onTop(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 10}, {0, 0, 1}));
    onTop.setHostBody(body);
    sketch::addRectangle(onTop, {-10, -10}, {10, 10});
    h.add(std::move(onTop));
    const Uuid high = h.plane(40);
    h.square(10, high);
    h.view();

    h.click({6, 6, 10});
    h.click({2, 2, 40}, true);
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_TRUE(h.loft()->hasHost());
    EXPECT_EQ(h.loft()->mode(), doc::ExtrudeMode::Join) << "it touches the block it grows";
    EXPECT_TRUE(h.active("mode:join"));
    ASSERT_TRUE(h.controller.triggerAction("loft:straight").ok());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(h.volume(0), 4000.0 + 7000.0, 1e-6);
    EXPECT_EQ(h.document.bodies()[0]->features().size(), 2u);
    // The Model panel offers Join / Cut for it too.
    const Uuid step = h.document.bodies()[0]->features()[1]->id();
    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        if (row.id == step) {
            listed = true;
            ASSERT_EQ(row.parameters.size(), 2u);
            EXPECT_EQ(row.parameters[1].key, "mode");
            EXPECT_EQ(row.parameters[1].valueText, "Join");
            EXPECT_EQ(row.detail, "2 profiles \xC2\xB7 Straight \xC2\xB7 Join");
        }
    EXPECT_TRUE(listed);
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(0), 4000.0, 1e-6);

    // New body, chosen.
    h.click({6, 6, 10});
    h.click({2, 2, 40}, true);
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_TRUE(h.controller.triggerAction("mode:new").ok());
    EXPECT_EQ(h.loft()->mode(), doc::ExtrudeMode::NewBody);
    EXPECT_TRUE(h.loft()->hasPreview());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(h.volume(0), 4000.0, 1e-6);
    EXPECT_NEAR(h.volume(1), frustum(400, 100, 30), 1e-6); // smooth: two profiles, the same frustum
    ASSERT_TRUE(h.controller.undo());
    ASSERT_EQ(h.document.bodies().size(), 1u);
}

TEST(LoftInteraction, AJoinThatMissesItsBodyBecomesANewBodyOnTheWorker)
{
    Harness h(/*async=*/true);
    // A block, and a sketch on its top face whose square lies beside it (not
    // over the block): the loft would not touch it.
    auto block = std::make_unique<doc::BoxFeature>();
    block->origin = {-10, -10, 0};
    block->size = {20, 20, 10};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid body = create->bodyId();
    ASSERT_TRUE(h.stack.push(std::move(create), h.document).ok());
    sketch::Sketch onTop(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 10}, {0, 0, 1}));
    onTop.setHostBody(body);
    sketch::addRectangle(onTop, {30, -5}, {40, 5});
    h.add(std::move(onTop));
    const Uuid high = h.plane(30);
    h.square(6, high, {35, 0});
    h.view();

    h.click({35, 0, 10});
    h.click({35, 0, 30}, true);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_TRUE(h.loft()->commitNeedsPreview());
    // Applied at once: the commit waits for the preview's verdict.
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(h.volume(0), 4000.0, 1e-6);
    EXPECT_NEAR(h.volume(1), frustum(100, 36, 20), 1e-6);
}

// Cut through a block: a square on the block's top face lofted to the same
// square on a plane under the block (a straight 10 x 10 prism through it).
// The automatic choice joins (the prism grows below the block), Cut takes
// the prism out, New body keeps it apart; a chosen profile tapped again comes
// out of the loft and, added back, lofts with the choices kept; the Model
// panel switches the step between Cut and Join.
TEST(LoftInteraction, CutThroughABlockAndSwitchToJoinInTheModelPanel)
{
    Harness h;
    auto block = std::make_unique<doc::BoxFeature>();
    block->origin = {0, 0, 0};
    block->size = {40, 40, 20};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid body = create->bodyId();
    ASSERT_TRUE(h.stack.push(std::move(create), h.document).ok());
    sketch::Sketch onTop(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 20}, {0, 0, 1}));
    onTop.setHostBody(body);
    sketch::addRectangle(onTop, onTop.plane().toLocal({15, 15, 20}), onTop.plane().toLocal({25, 25, 20}));
    h.add(std::move(onTop));
    const Uuid low = h.plane(-10);
    sketch::Sketch under = h.blank(low);
    sketch::addRectangle(under, under.plane().toLocal({15, 15, -10}), under.plane().toLocal({25, 25, -10}));
    h.add(std::move(under));

    // The top square from above, the one under the block from below.
    h.view();
    h.click({17, 17, 20});
    ASSERT_EQ(h.controller.selection().size(), 1u);
    h.controller.setStandardView(StandardView::Bottom, false);
    h.controller.fitAll(false);
    h.click({23, 23, -10}, true);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_EQ(h.loft()->mode(), doc::ExtrudeMode::Join);
    EXPECT_TRUE(h.active("mode:join"));

    ASSERT_TRUE(h.controller.triggerAction("mode:cut").ok());
    EXPECT_TRUE(h.active("mode:cut"));
    EXPECT_EQ(h.loft()->mode(), doc::ExtrudeMode::Cut);
    EXPECT_TRUE(h.loft()->hasPreview());
    EXPECT_TRUE(h.loft()->canCommit()) << h.loft()->error();

    // Shift-click the lower square: it comes out (one profile extrudes);
    // again: two profiles, Loft offered; Loft keeps Cut.
    h.click({23, 23, -10}, true);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.loft(), nullptr);
    h.click({23, 23, -10}, true);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_NE(h.loft(), nullptr);
    EXPECT_EQ(h.loft()->mode(), doc::ExtrudeMode::Cut) << "the choice stays while the profiles stay selected";

    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(h.volume(0), 32000.0 - 2000.0, 1e-6);
    const auto box = geom::boundingBox(h.document.bodies()[0]->shape());
    EXPECT_NEAR(box.min.z, 0, 1e-6) << "nothing added under the block";

    // The Model panel: Cut → Join.
    const Uuid step = h.document.bodies()[0]->features()[1]->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(step, "mode", "Join").ok());
    EXPECT_NEAR(h.volume(0), 32000.0 + 1000.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(h.document.bodies()[0]->shape()).min.z, -10, 1e-6);
    EXPECT_FALSE(h.controller.setFeatureParameter(step, "mode", "New body").ok());
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(0), 30000.0, 1e-6);
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(0), 32000.0, 1e-6);

    // New body, chosen: the prism alone.
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);
    h.click({17, 17, 20});
    h.controller.setStandardView(StandardView::Bottom, false);
    h.controller.fitAll(false);
    h.click({23, 23, -10}, true);
    ASSERT_TRUE(h.controller.triggerAction("loft").ok());
    ASSERT_TRUE(h.controller.triggerAction("mode:new").ok());
    EXPECT_TRUE(h.active("mode:new"));
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(h.volume(0), 32000.0, 1e-6);
    EXPECT_NEAR(h.volume(1), 3000.0, 1e-6);
}
