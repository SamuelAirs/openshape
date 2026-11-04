// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Find outline (docs/MEASURE.md, "Photo"): a generated photo of an L bracket
// (80 x 60 mm, arms 20 mm wide, a 10 mm hole), dark on light paper, beside a
// playing card turned on the paper, taken at a slant, with a soft shadow,
// falling light, blur and noise. Picked (phone), the card found, Straighten,
// Outline, Find outline: the outline's six sides, its width x height along
// the part's long side and the hole's diameter, in millimetres, against the
// truth; a point dragged (the outline stays the found one), Find outline
// again; Make sketch: six lines and the hole's circle, true size, two
// regions; the plate with its hole extruded 5 mm and its volume checked.
// Then the desktop: back to Measure, Find outline clicked. Pictures
// measure_autooutline_*.png.

#include "app/AcceptanceRunner.h"
#include "core/Math.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"
#include "interaction/InteractionController.h"
#include "interaction/SketchSession.h"
#include "measure/Homography.h"
#include "measure/OutlineSynthetic.h"
#include "measure/ScreenScale.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QLineF>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

struct Size {
    QString name;
    int width = 0;
    int height = 0;
    double safe[4] = {0, 0, 0, 0}; // top, right, bottom, left
    bool touch = true;
};

const Size kPhone{QStringLiteral("phone"), 402, 874, {62, 0, 34, 0}, true};
const Size kDesktop{QStringLiteral("desktop"), 1400, 900, {0, 0, 0, 0}, false};

// The bracket on the paper (mm): the long arm from x 110 to 190, the short
// one up to y 60, the hole in the long arm.
const Vec2 kHole{160, 10};
constexpr double kHoleDiameter = 10;
constexpr double kWidth = 80, kHeight = 60;
const double kArea = 80 * 20 + 20 * 40 - kPi * 25;
constexpr double kExtrude = 5;

struct State {
    std::unique_ptr<QTemporaryDir> dir;
    QString photo;
    std::array<QPointF, 4> cardCorners{};
    int picks = 0;
    QPointF dragFrom;
    QPointF pointBefore;
};
using StatePtr = std::shared_ptr<State>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

QString say(const QString& text) { return QStringLiteral("measureautooutline: ") + text; }

ui::MeasureController& measureOf(AcceptanceRunner& r) { return *r.app().measure(); }

QQuickItem* item(AcceptanceRunner& r, const QString& name) { return r.findItem(name); }

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = item(r, name);
    return i && i->isVisible();
}

QString text(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = item(r, name);
    return i ? i->property("text").toString() : QString();
}

QVariant prop(AcceptanceRunner& r, const QString& name, const char* property)
{
    QQuickItem* i = item(r, name);
    return i ? i->property(property) : QVariant();
}

QPointF center(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = item(r, name);
    return i ? i->mapRectToScene(QRectF(0, 0, i->width(), i->height())).center() : QPointF();
}

void applySize(AcceptanceRunner& r, const Size& s)
{
    r.app().setTouchMode(s.touch);
    r.resizeWindow(s.width, s.height);
    const bool none = s.safe[0] == 0 && s.safe[1] == 0 && s.safe[2] == 0 && s.safe[3] == 0;
    r.window()->setProperty("simulatedSafeArea", none ? QVariant() : QVariant(QVariantList{s.safe[0], s.safe[1], s.safe[2], s.safe[3]}));
}

ui::MeasureDevices standIns(const StatePtr& st)
{
    ui::MeasureDevices d;
    d.cameraAvailable = [] { return true; };
    d.pickPhoto = [st](QWindow*, ui::PhotoOrigin, const ui::CameraOptions&, const QString& folder, const QRectF&,
                       std::function<void(const ui::PhotoResult&)> finished) {
        ++st->picks;
        QDir().mkpath(folder);
        const QString out = folder + QStringLiteral("/bracket_%1.png").arg(st->picks);
        ui::PhotoResult result;
        result.outcome = QImage(st->photo).save(out, "PNG") ? ui::PhotoResult::Outcome::Picked : ui::PhotoResult::Outcome::Failed;
        result.file = out;
        QTimer::singleShot(0, QCoreApplication::instance(), [finished, result] { finished(result); });
    };
    d.detectRectangle = [st](const QString&) -> std::optional<ui::RectangleCorners> { return st->cardCorners; };
    return d;
}

// The bracket and a playing card turned 12 degrees on light paper, on a
// wooden table, photographed at a slant.
bool makePhoto(const StatePtr& st)
{
    measure::SyntheticScene scene;
    const Vec2 c{44.45, 31.75};
    const double a = 12 * kPi / 180;
    const std::array<Vec2, 4> flat{Vec2{0, 0}, Vec2{measure::kPlayingCard.longMm, 0},
                                   Vec2{measure::kPlayingCard.longMm, measure::kPlayingCard.shortMm}, Vec2{0, measure::kPlayingCard.shortMm}};
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 d = flat[i] - c;
        scene.card[i] = c + Vec2{std::cos(a) * d.x - std::sin(a) * d.y, std::sin(a) * d.x + std::cos(a) * d.y};
    }
    scene.parts.push_back({[](Vec2 p) {
                               return measure::subtract(measure::unite(measure::boxDistance(p, {150, 10}, {40, 10}),
                                                                       measure::boxDistance(p, {120, 30}, {10, 30})),
                                                        measure::circleDistance(p, kHole, kHoleDiameter / 2));
                           },
                           {52, 56, 62}});
    const std::array<Vec2, 4> surface{Vec2{-60, -80}, Vec2{250, -80}, Vec2{250, 150}, Vec2{-60, 150}};
    const std::array<Vec2, 4> image{Vec2{260, 190}, Vec2{1760, 120}, Vec2{1960, 1440}, Vec2{40, 1330}};
    const auto h = measure::Homography::fromQuads(surface, image);
    if (!h)
        return false;
    constexpr int kW = 2000, kH = 1500;
    const std::vector<std::uint8_t> pixels = measure::renderSyntheticPhoto(scene, *h, kW, kH);
    const QImage img(pixels.data(), kW, kH, kW * 4, QImage::Format_RGBA8888);
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 q = h->map(scene.card[i]);
        st->cardCorners[i] = QPointF(q.x, q.y);
    }
    st->photo = st->dir->filePath(QStringLiteral("bracket.png"));
    return img.save(st->photo);
}

QQuickItem* photoItem(AcceptanceRunner& r) { return item(r, QStringLiteral("measurePhoto")); }

// The found outline's report as the readout has it.
QVariantMap report(AcceptanceRunner& r)
{
    QQuickItem* photo = photoItem(r);
    if (!photo)
        return {};
    // The outline on screen is the found one (its points perhaps dragged;
    // its arcs' marks and holes are the controller's).
    const QVariant found = photo->property("found");
    if (!found.isValid() || found.isNull())
        return {};
    return measureOf(r).profileReport(photo->property("points").toList(), measureOf(r).foundOutline());
}

// Checks the found outline against the truth (mm).
void checkFound(AcceptanceRunner& r, const QString& where)
{
    const QVariantMap rep = report(r);
    const double w = rep.value(QStringLiteral("width")).toDouble(), h = rep.value(QStringLiteral("height")).toDouble();
    const double area = rep.value(QStringLiteral("area")).toDouble();
    const QVariantList holes = rep.value(QStringLiteral("holes")).toList();
    const QVariantList sides = rep.value(QStringLiteral("sides")).toList();
    const double hole = holes.size() == 1 ? holes[0].toMap().value(QStringLiteral("diameter")).toDouble() : 0;
    r.check(sides.size() == 6, say(where + QStringLiteral(": six sides")), QString::number(sides.size()));
    r.check(std::abs(w - kWidth) < 0.4 && std::abs(h - kHeight) < 0.4, say(where + QStringLiteral(": 80 x 60 mm within 0.4 mm")),
            QStringLiteral("%1 x %2").arg(w, 0, 'f', 3).arg(h, 0, 'f', 3));
    r.check(holes.size() == 1 && holes[0].toMap().value(QStringLiteral("circle")).toBool() && std::abs(hole - kHoleDiameter) < 0.3,
            say(where + QStringLiteral(": one round hole, 10 mm within 0.3 mm")), QString::number(hole, 'f', 3));
    r.check(std::abs(area - kArea) < 0.01 * kArea, say(where + QStringLiteral(": the area within 1 %")), QString::number(area, 'f', 1));
    if (sides.size() == 6) {
        QStringList list;
        for (const QVariant& v : sides)
            list << QString::number(v.toDouble(), 'f', 2);
        r.check(true, say(where + QStringLiteral(": sides (mm)")), list.join(QStringLiteral(", ")));
    }
}

const interact::SketchSession* sketchOf(AcceptanceRunner& r) { return r.app().interaction().sketchSession(); }

QPointF onScreen(AcceptanceRunner& r, const Vec3& world)
{
    const Vec2 p = r.app().interaction().camera().project(world);
    return {p.x, p.y};
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<State>();
    Steps steps;

    steps.push_back([&r, st] {
        st->dir = std::make_unique<QTemporaryDir>();
        r.check(st->dir->isValid() && makePhoto(st), say("the test photo is drawn"), st->photo);
        r.app().newDocument();
        auto& m = measureOf(r);
        m.setDevices(standIns(st));
        m.clearPhoto();
        m.setReference(QStringLiteral("card"));
        if (QQuickItem* screen = item(r, QStringLiteral("measureScreen"))) {
            QMetaObject::invokeMethod(screen, "close");
            QMetaObject::invokeMethod(screen, "resetForTest");
        }
        applySize(r, kPhone);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("tap Measure")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_photo")), say("tap Photo")); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoChoose")), say("tap Choose photo")); });
    steps.push_back([&r, st] {
        r.check(measureOf(r).waitForPhoto(15000) && st->picks == 1 && measureOf(r).cornersDetected(), say("the photo, the card found"));
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoStraighten")), say("tap Straighten")); });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(20000), say("straightened")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measurePhoto"), "stage").toString() == QStringLiteral("measure"), say("the measuring step"));
        r.check(!shown(r, QStringLiteral("photoFindOutline")), say("no Find outline while measuring a distance"));
        r.check(r.tapItem(QStringLiteral("photoMode_outline")), say("tap Outline"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoFindOutline")), say("Find outline is offered"));
        r.check(shown(r, QStringLiteral("photoOutlineTip")), say("the tip for the best result shows"));
        r.screenshot(QStringLiteral("measure_autooutline_ready_phone"));
        r.check(r.tapItem(QStringLiteral("photoFindOutline")), say("tap Find outline"));
    });
    steps.push_back([&r] {
        r.check(measureOf(r).waitForPhoto(30000), say("the outline is found (in the background)"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        QQuickItem* photo = photoItem(r);
        const QVariantMap found = measureOf(r).foundOutline();
        r.check(found.value(QStringLiteral("valid")).toBool(), say("found"), found.value(QStringLiteral("message")).toString());
        r.check(photo && photo->property("closed").toBool() && photo->property("points").toList().size() >= 6,
                say("its points are the outline's, closed"));
        r.check(found.value(QStringLiteral("maxError")).toDouble() < 0.3, say("fitted within 0.3 mm"),
                QString::number(found.value(QStringLiteral("maxError")).toDouble(), 'f', 3));
        checkFound(r, QStringLiteral("phone"));
        r.check(shown(r, QStringLiteral("photoHole_0")), say("the hole is drawn"));
        r.check(!shown(r, QStringLiteral("photoOutlineTip")), say("the tip goes"));
        r.check(text(r, QStringLiteral("measureReadoutCaption")).startsWith(QStringLiteral("Width")), say("the width read out"),
                text(r, QStringLiteral("measureReadoutCaption")));
        r.check(shown(r, QStringLiteral("measureAction_outlineSketch")), say("Make sketch is offered"));
        r.check(prop(r, QStringLiteral("measurePhoto"), "dims").toList().size() == 1 && shown(r, QStringLiteral("photoDim_0")),
                say("the found outline is a dimension, listed under the photo"));
        r.screenshot(QStringLiteral("measure_autooutline_found_phone"));
    });
    // A point dragged: the outline stays the found one (its holes and arcs).
    steps.push_back([&r, st] {
        st->dragFrom = center(r, QStringLiteral("photoPoint_1"));
        st->pointBefore = photoItem(r)->property("points").toList().value(1).toPointF();
        r.touchPress(st->dragFrom);
    });
    for (int i = 1; i <= 6; ++i)
        steps.push_back([&r, st, i] { r.touchMove(st->dragFrom + QPointF(14, 10) * (i / 6.0)); });
    steps.push_back([&r, st] { r.touchRelease(st->dragFrom + QPointF(14, 10)); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        QQuickItem* photo = photoItem(r);
        const QPointF now = photo->property("points").toList().value(1).toPointF();
        r.check(QLineF(now, st->pointBefore).length() > 3, say("a point follows the finger"),
                QStringLiteral("%1 px").arg(QLineF(now, st->pointBefore).length()));
        r.check(!report(r).isEmpty() && report(r).value(QStringLiteral("holes")).toList().size() == 1,
                say("still the found outline (its hole kept)"));
        r.screenshot(QStringLiteral("measure_autooutline_dragged_phone"));
        r.check(r.tapItem(QStringLiteral("photoFindOutline")), say("tap Find outline again"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(30000), say("found again")); });
    wait(steps, 2);
    steps.push_back([&r] {
        checkFound(r, QStringLiteral("again"));
        const QVariantList dims = prop(r, QStringLiteral("measurePhoto"), "dims").toList();
        r.check(dims.size() == 1 && dims[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("outline")
                    && dims[0].toMap().value(QStringLiteral("closed")).toBool(),
                say("one closed outline dimension (Find outline again replaces it)"), QString::number(dims.size()));
        r.check(r.tapItem(QStringLiteral("measureAction_outlineSketch")), say("tap Make sketch"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(!m.madeLog().isEmpty() && m.madeLog().last().startsWith(QStringLiteral("profile 2 ")), say("a sketch of two loops is asked for"),
                m.madeLog().isEmpty() ? QString() : m.madeLog().last());
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        const QVariantList loops = m.lastProfileLoops();
        if (!loops.isEmpty())
            for (const QVariant& v : loops.front().toMap().value(QStringLiteral("points")).toList()) {
                minX = std::min(minX, v.toPointF().x());
                maxX = std::max(maxX, v.toPointF().x());
                minY = std::min(minY, v.toPointF().y());
                maxY = std::max(maxY, v.toPointF().y());
            }
        r.check(std::abs(maxX - minX - kWidth) < 0.4 && std::abs(maxY - minY - kHeight) < 0.4 && std::abs(minX + maxX) < 1e-6
                    && std::abs(minY + maxY) < 1e-6,
                say("true size, the long side along X, centered"), QStringLiteral("%1 x %2").arg(maxX - minX).arg(maxY - minY));
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("measureScreen")) && r.app().sketchMode(), say("the workspace closes into the new sketch"));
        const auto* s = sketchOf(r);
        r.check(s && s->sketch().lines().size() == 6 && s->sketch().arcs().empty(), say("the sketch: six lines"),
                s ? QStringLiteral("%1 lines, %2 arcs").arg(s->sketch().lines().size()).arg(s->sketch().arcs().size()) : QString());
        double d = 0;
        if (s && s->sketch().circles().size() == 1)
            d = 2 * s->sketch().circles().begin()->second.radius;
        r.check(std::abs(d - kHoleDiameter) < 0.3, say("and the hole's circle, 10 mm"), QString::number(d, 'f', 3));
        r.check(s && s->regions().size() == 2, say("two regions: the plate with its hole, and the hole"));
        r.screenshot(QStringLiteral("measure_autooutline_sketch_phone"));
        applySize(r, kDesktop);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        const auto* s = sketchOf(r);
        if (s && !s->regions().empty())
            r.click(onScreen(r, s->regions().front().interiorPoint));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const auto* s = sketchOf(r);
        r.check(s && s->selectedRegion().has_value(), say("a click inside the plate selects it"));
        r.check(r.clickItem(QStringLiteral("sketchAction_extrude")), say("Extrude"));
    });
    wait(steps, 5);
    steps.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Extrude"), say("extruding"), r.app().operationTitle());
        r.type(QString::number(kExtrude));
        r.key(Qt::Key_Return);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        const double volume = r.app().bodyCount() == 1 ? r.bodyVolume() : 0;
        const double expected = kArea * kExtrude;
        r.check(std::abs(volume - expected) < 0.015 * expected, say("the part with its hole: 5 mm thick, the volume within 1.5 %"),
                QStringLiteral("%1 mm3 (%2 expected)").arg(volume, 0, 'f', 1).arg(expected, 0, 'f', 1));
        if (r.app().bodyCount() == 1) {
            const auto bb = geom::boundingBox(r.body(0).shape());
            r.check(std::abs(bb.max.x - bb.min.x - kWidth) < 0.4 && std::abs(bb.max.y - bb.min.y - kHeight) < 0.4
                        && std::abs(bb.max.z - bb.min.z - kExtrude) < 1e-6,
                    say("80 x 60 x 5 mm"),
                    QStringLiteral("%1 x %2 x %3").arg(bb.max.x - bb.min.x).arg(bb.max.y - bb.min.y).arg(bb.max.z - bb.min.z));
        }
        r.app().interaction().fitAll(false);
        r.screenshot(QStringLiteral("measure_autooutline_part_desktop"));
    });
    // The desktop: back to Measure, Find outline clicked.
    steps.push_back([&r] {
        if (r.app().sketchMode())
            r.app().finishSketch();
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureButton")), say("desktop: click Measure")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measurePhoto"), "stage").toString() == QStringLiteral("measure"), say("desktop: the straightened photo is still there"));
        r.check(shown(r, QStringLiteral("photoFindOutline")) && r.clickItem(QStringLiteral("photoFindOutline")), say("desktop: click Find outline"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(30000), say("desktop: found")); });
    wait(steps, 2);
    steps.push_back([&r] {
        checkFound(r, QStringLiteral("desktop"));
        r.screenshot(QStringLiteral("measure_autooutline_found_desktop"));
        if (QQuickItem* screen = item(r, QStringLiteral("measureScreen")))
            QMetaObject::invokeMethod(screen, "close");
        measureOf(r).clearPhoto();
        r.app().newDocument();
    });
    wait(steps, 2);
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measureautooutline"), 114, steps});

} // namespace
} // namespace os::app
