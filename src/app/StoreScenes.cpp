// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "app/StoreScenes.h"

#include "core/Log.h"
#include "core/Math.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/SketchSession.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QUrl>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace os::app {
namespace {

using interact::Key;
using interact::PointerButton;
using interact::PointerDevice;
using interact::PointerEvent;
using interact::SketchTool;

// Drives the interaction core as a user would: clicks at model points (placed
// from the camera, so they land at any window size), typed values, tools.
class Builder {
public:
    explicit Builder(ui::AppController& app)
        : app_(app)
        , ix_(app.interaction())
    {
    }

    interact::InteractionController& ix() { return ix_; }
    ui::AppController& app() { return app_; }

    Vec2 screenOf(const Vec3& world) const { return ix_.camera().project(world); }

    void move(const Vec3& world)
    {
        ix_.pointerMove({PointerDevice::Mouse, PointerButton::None, screenOf(world), {}});
    }

    // A click at a model point (with Shift: adds to the selection).
    void click(const Vec3& world, bool add = false)
    {
        PointerEvent e;
        e.position = screenOf(world);
        e.modifiers.shift = add;
        ix_.pointerMove({PointerDevice::Mouse, PointerButton::None, e.position, {}});
        ix_.pointerPress(e);
        ix_.pointerRelease(e);
    }

    void escape()
    {
        ix_.keyPress(Key::Escape);
        ix_.keyPress(Key::Escape);
    }

    // The iso view, the parts large on the screen (clicks at every step land
    // on a comfortable size, on a phone too).
    void iso()
    {
        ix_.setStandardView(StandardView::Isometric, false);
        ix_.fitAll(false);
        frameForScreen();
    }

    // Wheel notches at a model point (positive zooms in).
    void zoom(const Vec3& at, double notches) { ix_.wheel(screenOf(at), notches); }

    // Looks from yaw / pitch (degrees; yaw -45, pitch 35.3 is the iso view),
    // everything in view.
    void look(double yawDegrees, double pitchDegrees)
    {
        ix_.setViewAngles(yawDegrees * kPi / 180, pitchDegrees * kPi / 180, false);
        ix_.fitAll(false);
    }

    // The box [lo, hi] moved (two-finger pan) to `at` (a share of the view's
    // width and height) and zoomed there until it takes `widthShare` of the
    // view's width or `heightShare` of its height, whichever comes first:
    // fitAll leaves a small part small on a tall phone screen, and centered
    // under the panels.
    void frameBox(const Vec3& lo, const Vec3& hi, double widthShare, double heightShare, Vec2 at)
    {
        const Vec2 view = ix_.camera().viewportSize;
        const Vec2 target{view.x * at.x, view.y * at.y};
        auto extent = [&](Vec2& middle) {
            double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
            for (int i = 0; i < 8; ++i) {
                const Vec2 p = screenOf({i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z});
                x0 = std::min(x0, p.x);
                x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y);
                y1 = std::max(y1, p.y);
            }
            middle = {(x0 + x1) / 2, (y0 + y1) / 2};
            return std::max((x1 - x0) / (view.x * widthShare), (y1 - y0) / (view.y * heightShare));
        };
        // Pan and zoom in turns: perspective moves the middle as it zooms.
        for (int round = 0; round < 4; ++round) {
            Vec2 middle;
            (void)extent(middle);
            ix_.twoFingerPan(middle, target);
            for (int i = 0; i < 80 && extent(middle) < 0.97; ++i)
                ix_.wheel(target, 0.25);
            for (int i = 0; i < 80 && extent(middle) > 1.03; ++i)
                ix_.wheel(target, -0.25);
        }
    }

    // The bodies' bounding box.
    bool bodiesBox(Vec3& lo, Vec3& hi) const
    {
        const auto& bodies = app_.document().bodies();
        lo = {1e9, 1e9, 1e9};
        hi = {-1e9, -1e9, -1e9};
        for (const auto& body : bodies) {
            const auto bb = geom::boundingBox(body->shape());
            lo = {std::min(lo.x, bb.min.x), std::min(lo.y, bb.min.y), std::min(lo.z, bb.min.z)};
            hi = {std::max(hi.x, bb.max.x), std::max(hi.y, bb.max.y), std::max(hi.z, bb.max.z)};
        }
        return !bodies.empty();
    }

    // frameBox for the screen: a phone held upright has the width to fill
    // between the top bar and the tool strip; an iPad (landscape) has the
    // tools on the left and the Model panel on the right. `scale` < 1 leaves
    // room (for a plane above the part); `down` moves it lower on a phone
    // (below an open Model panel).
    void frameForScreen(const Vec3& lo, const Vec3& hi, double scale = 1.0, double down = 0.0)
    {
        const Vec2 view = ix_.camera().viewportSize;
        if (view.y > view.x)
            frameBox(lo, hi, 0.84 * scale, 0.4 * scale, {0.5, 0.52 + down});
        else
            frameBox(lo, hi, 0.5 * scale, 0.58 * scale, {0.46, 0.52});
    }
    bool portrait() const { return ix_.camera().viewportSize.y > ix_.camera().viewportSize.x; }
    void frameForScreen(double scale = 1.0, double down = 0.0)
    {
        Vec3 lo, hi;
        if (bodiesBox(lo, hi))
            frameForScreen(lo, hi, scale, down);
    }

    // Types a value into the active tool and applies it, then looks from the
    // front corner again with everything in view.
    bool apply(const char* value)
    {
        ix_.setValueText(value);
        return commit(value);
    }

    // Applies the active tool as it is.
    bool commit(const char* what = "the tool")
    {
        (void)ix_.waitForPreview();
        const Status status = ix_.commitOperation();
        if (!status)
            OS_LOG(Warning, App) << "store scene: applying " << what << " failed: " << status.userMessage();
        iso();
        return status.ok();
    }

    // A sketch on the ground: a rectangle from the origin, w x h, finished.
    void groundRectangle(double w, double h)
    {
        (void)ix_.startSketch();
        ix_.skipAnimation();
        ix_.setSketchTool(SketchTool::Rectangle);
        click({0, 0, 0});
        move({w * 0.6, h * 0.6, 0});
        typeSketch(std::to_string(int(w)));
        ix_.sketchSession()->focusNextInput();
        typeSketch(std::to_string(int(h)));
        ix_.keyPress(Key::Enter);
        ix_.finishSketch();
        ix_.setStandardView(StandardView::Isometric, false);
        ix_.fitAll(false);
        frameForScreen({0, 0, 0}, {w, h, 0});
    }

    // Circles of diameter `d` drawn on the selected face's sketch at model
    // points `centers` (on the face), the sketch finished.
    void circlesOnFace(const Vec3& face, double d, std::initializer_list<Vec3> centers, const Vec3& radial)
    {
        click(face);
        (void)ix_.startSketch();
        ix_.skipAnimation();
        for (const Vec3& c : centers) {
            ix_.setSketchTool(SketchTool::Circle);
            click(c);
            move(c + radial * (d * 0.4));
            typeSketch(number(d));
            ix_.keyPress(Key::Enter);
        }
        ix_.finishSketch();
        iso();
    }

    void typeSketch(const std::string& text)
    {
        if (auto* session = ix_.sketchSession())
            session->typeIntoInput(text);
    }

    // Clicks the first of `points` whose click selects a face described as
    // `kind` (e.g. "Cylindrical face"): thin walls are easy to miss at some
    // window sizes and angles.
    bool clickFace(const std::vector<Vec3>& points, const std::string& kind)
    {
        for (const Vec3& p : points) {
            escape();
            click(p);
            if (ix_.selectionSummary().rfind(kind, 0) == 0)
                return true;
        }
        OS_LOG(Warning, App) << "store scene: no click selected a " << kind;
        return false;
    }

    static std::string number(double v)
    {
        std::string s = std::to_string(v);
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.')
            s.pop_back();
        return s;
    }

private:
    ui::AppController& app_;
    interact::InteractionController& ix_;
};

// ---- The parts ---------------------------------------------------------------------------

// Rounds the four upright edges of the box [0, w] x [0, d] (at height z)
// with radius r; the back edge is picked from behind, as a user turns the
// view to reach it.
void roundUprightEdges(Builder& b, double w, double d, double z, const char* r)
{
    b.click({w, 0, z});
    b.click({0, 0, z}, true);
    b.click({w, d, z}, true);
    b.ix().twoFingerRotate(kPi / 0.008, 0); // half a turn (the camera orbits 0.008 rad per pixel)
    b.click({0, d, z}, true);
    b.apply(r);
}

// A 60 x 40 x 22 mm project box: rounded corners, 2 mm walls, four
// countersunk M3 holes in the floor and a 10 mm cable hole in one end.
void buildEnclosure(Builder& b)
{
    b.groundRectangle(60, 40);
    b.click({30, 20, 0});
    b.apply("22");
    roundUprightEdges(b, 60, 40, 11, "6");
    b.click({30, 20, 22});
    (void)b.ix().triggerAction("shell");
    b.apply("2");
    // Screw holes through the floor (its top is at z = 2), placed looking
    // down into the box (the walls hide its corners from the side).
    b.ix().setStandardView(StandardView::Top, false);
    b.ix().fitAll(false);
    b.frameForScreen();
    b.click({30, 20, 2});
    (void)b.ix().triggerAction("hole");
    for (const Vec3& p : {Vec3{14, 12, 2}, Vec3{46, 12, 2}, Vec3{46, 28, 2}, Vec3{14, 28, 2}})
        b.click(p);
    (void)b.ix().triggerAction("head:countersink");
    b.commit("the screw holes");
    // The cable hole in the right end (x = 60), cut through its wall.
    b.circlesOnFace({60, 20, 16}, 10, {{60, 20, 11}}, {0, 1, 0});
    b.click({60, 20, 11});
    b.apply("-4");
}

// The hole's wall selected and a diameter with print clearance typed (not
// applied): the value box and its arrow on the part.
void typeHoleDiameter(Builder& b)
{
    // Zoomed in to reach the 2 mm wall, as a user would, then out again.
    const Vec3 middle{60, 20, 11};
    int steps = 0;
    for (; steps < 40 && b.ix().camera().pixelSize({59, 23, 7.5}) > 1.0 / 25; ++steps)
        b.zoom(middle, 1);
    // The part of the hole's wall that faces the viewer (seen from the
    // front right, above): from its bottom round to the back.
    std::vector<Vec3> wall;
    for (double degrees : {45.0, 30.0, 60.0, 15.0, 75.0})
        for (double x : {59.0, 59.4, 58.6}) {
            const double a = degrees * kPi / 180;
            wall.push_back({x, 20 + 5 * std::sin(a), 11 - 5 * std::cos(a)});
        }
    b.clickFace(wall, "Cylindrical face");
    b.zoom(middle, -steps);
    b.look(-40, 46);
    b.frameForScreen();
    b.ix().setValueText("10.4");
}

// A 70 x 24 x 3 mm key tag with rounded corners and a key-ring hole.
void buildTag(Builder& b)
{
    b.groundRectangle(70, 24);
    b.click({35, 12, 0});
    b.apply("20");
    roundUprightEdges(b, 70, 24, 10, "6");
    b.click({40, 12, 20});
    b.apply("3");
    b.circlesOnFace({40, 12, 3}, 5, {{7.5, 12, 3}}, {0, 1, 0});
    b.click({7.5, 12, 3});
    b.apply("-3");
}

// Raised bold text on the tag, being edited: size and depth in the value box.
void editTagText(Builder& b, bool keepOpen)
{
    b.click({40, 12, 3});
    (void)b.ix().triggerAction("text");
    (void)b.ix().setOperationText("GARAGE");
    (void)b.ix().triggerAction("bold");
    (void)b.ix().triggerAction("field:size");
    b.ix().setValueText("7");
    (void)b.ix().triggerAction("field:depth");
    b.ix().setValueText("1");
    (void)b.ix().waitForPreview();
    if (!keepOpen) {
        (void)b.ix().commitOperation();
        b.escape();
        b.iso();
    }
}

// A 50 x 50 x 6 mm flange plate with rounded corners and a 12 mm bore.
void buildPlate(Builder& b)
{
    b.groundRectangle(50, 50);
    b.click({25, 25, 0});
    b.apply("20");
    roundUprightEdges(b, 50, 50, 10, "5");
    b.click({25, 25, 20});
    b.apply("6");
    b.circlesOnFace({40, 10, 6}, 12, {{25, 25, 6}}, {1, 0, 0});
    b.click({25, 25, 6});
    b.apply("-6");
}

// An axis through the bore (from its rim), then the Plane tool placing a
// plane 25 mm above the plate.
void placePlanes(Builder& b, bool keepOpen)
{
    b.escape();
    b.click({25, 19, 6}); // the bore's rim, nearest the viewer
    (void)b.ix().runTool("axis");
    b.commit("the axis");
    b.escape();
    (void)b.ix().runTool("plane");
    b.click({42, 8, 6});
    b.ix().setValueText("25");
    (void)b.ix().waitForPreview();
    if (!keepOpen) {
        b.commit("the plane");
        b.escape();
    }
}

// A mounting plate's sketch: an 80 x 50 outline, four 5 mm screw holes and
// a 20 mm opening, sizes typed; the Select tool, ready to change a size.
void drawPlateSketch(Builder& b)
{
    (void)b.ix().startSketch();
    b.ix().skipAnimation();
    b.frameForScreen({0, 0, 0}, {80, 50, 0}, 0.9);
    b.ix().setSketchTool(SketchTool::Rectangle);
    b.click({0, 0, 0});
    b.move({50, 30, 0});
    b.typeSketch("80");
    b.ix().sketchSession()->focusNextInput();
    b.typeSketch("50");
    b.ix().keyPress(Key::Enter);
    for (const Vec3& c : {Vec3{8, 8, 0}, Vec3{72, 8, 0}, Vec3{72, 42, 0}, Vec3{8, 42, 0}}) {
        b.ix().setSketchTool(SketchTool::Circle);
        b.click(c);
        b.move(c + Vec3{2, 0, 0});
        b.typeSketch("5");
        b.ix().keyPress(Key::Enter);
    }
    b.ix().setSketchTool(SketchTool::Circle);
    b.click({40, 25, 0});
    b.move({48, 25, 0});
    b.typeSketch("20");
    b.ix().keyPress(Key::Enter);
    b.ix().setSketchTool(SketchTool::Select);
    b.move({100, -30, 0}); // the pointer off the drawing: nothing under it lights up
}

// A round spacer, 20 mm across and 10 mm high, with an 8.4 mm hole.
void buildSpacer(Builder& b)
{
    (void)b.ix().startSketch();
    b.ix().skipAnimation();
    b.frameForScreen({-10, -10, 0}, {10, 10, 0});
    for (const char* d : {"20", "8.4"}) {
        b.ix().setSketchTool(SketchTool::Circle);
        b.click({0, 0, 0});
        b.move({3, 0, 0});
        b.typeSketch(d);
        b.ix().keyPress(Key::Enter);
    }
    b.ix().finishSketch();
    b.ix().setStandardView(StandardView::Isometric, false);
    b.ix().fitAll(false);
    b.frameForScreen({-10, -10, 0}, {10, 10, 0});
    b.click({7, 0, 0}); // the ring between the circles
    b.apply("10");
}

// A pen cup: 60 mm across, 90 mm high, 2 mm walls.
void buildCup(Builder& b)
{
    (void)b.ix().startSketch();
    b.ix().skipAnimation();
    b.frameForScreen({-30, -30, 0}, {30, 30, 0});
    b.ix().setSketchTool(SketchTool::Circle);
    b.click({0, 0, 0});
    b.move({10, 0, 0});
    b.typeSketch("60");
    b.ix().keyPress(Key::Enter);
    b.ix().finishSketch();
    b.ix().setStandardView(StandardView::Isometric, false);
    b.ix().fitAll(false);
    b.frameForScreen({-30, -30, 0}, {30, 30, 0});
    b.click({0, 0, 0});
    b.apply("90");
    b.click({0, 0, 90});
    (void)b.ix().triggerAction("shell");
    b.apply("2");
}

// Saves the document as a project named `name` (into the app folder when
// there is one, as on an iPhone) and starts a new one.
void saveAs(ui::AppController& app, const QString& name, const QString& dataDir)
{
    bool ok = false;
    if (app.savesToAppFolder()) {
        ok = app.saveInAppFolder(name);
    } else {
        const QString dir = (dataDir.isEmpty() ? QDir::tempPath() : dataDir) + QStringLiteral("/projects");
        QDir().mkpath(dir);
        ok = app.saveProjectAs(QUrl::fromLocalFile(dir + QLatin1Char('/') + name + QStringLiteral(".openshape")));
    }
    if (!ok)
        OS_LOG(Warning, App) << "store scene: could not save " << name.toStdString();
    app.newDocument();
}

} // namespace

QStringList storeSceneNames()
{
    return {QStringLiteral("store-enclosure"), QStringLiteral("store-text"), QStringLiteral("store-planes"),
            QStringLiteral("store-sketch"),    QStringLiteral("store-history"), QStringLiteral("store-home")};
}

bool isStoreScene(const QString& name)
{
    return storeSceneNames().contains(name);
}

bool runStoreScene(ui::AppController& app, const QString& name, const QString& dataDir)
{
    if (!isStoreScene(name))
        return false;
    Builder b(app);
    b.ix().fitAll(false);
    if (name == QLatin1String("store-enclosure")) {
        buildEnclosure(b);
        typeHoleDiameter(b);
    } else if (name == QLatin1String("store-text")) {
        buildTag(b);
        editTagText(b, true);
        b.look(-35, 50);
        b.frameForScreen();
    } else if (name == QLatin1String("store-planes")) {
        buildPlate(b);
        placePlanes(b, true);
        b.look(-40, 28);
        b.frameForScreen({0, 0, 0}, {50, 50, 31}, 0.95);
    } else if (name == QLatin1String("store-sketch")) {
        drawPlateSketch(b);
    } else if (name == QLatin1String("store-history")) {
        buildEnclosure(b);
        b.escape();
        b.look(135, 40); // from the back left: another side than the first picture
        if (b.portrait())
            b.frameForScreen(0.72, 0.19); // on a phone: below the open Model panel
        else
            b.frameForScreen();
    } else if (name == QLatin1String("store-home")) {
        // Saved oldest first: Home lists the newest first.
        buildCup(b);
        saveAs(app, QStringLiteral("Pen cup"), dataDir);
        buildSpacer(b);
        saveAs(app, QStringLiteral("Spacer M8"), dataDir);
        buildPlate(b);
        placePlanes(b, false);
        saveAs(app, QStringLiteral("Flange plate"), dataDir);
        drawPlateSketch(b);
        b.ix().finishSketch();
        b.ix().setStandardView(StandardView::Isometric, false);
        b.ix().fitAll(false);
        b.frameForScreen({0, 0, 0}, {80, 50, 0});
        b.click({20, 25, 0}); // the plate around the holes, extruded 4 mm
        b.apply("4");
        b.escape();
        saveAs(app, QStringLiteral("Mounting plate"), dataDir);
        buildTag(b);
        editTagText(b, false);
        saveAs(app, QStringLiteral("Key tag"), dataDir);
        buildEnclosure(b);
        b.escape();
        saveAs(app, QStringLiteral("Project box"), dataDir);
        app.setHomeVisible(true);
    }
    (void)b.ix().waitForPreview();
    return true;
}

QString storeSceneOpenStep(ui::AppController& app, const QString& name)
{
    if (name != QLatin1String("store-history") || app.document().bodies().empty())
        return {};
    // The rounded corners: their radius can be changed right there.
    for (const auto& feature : app.document().bodies().front()->features())
        if (feature->kind() == doc::FeatureKind::Fillet)
            return QString::fromStdString(feature->id().toString());
    return {};
}

} // namespace os::app
