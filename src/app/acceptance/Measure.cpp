// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Measure (docs/MEASURE.md): the workspace with stand-in hardware (a hinge
// and gravity the run moves, a photo picker and a rectangle finder, ARKit
// returning a box, an outline and a distance) and a fake clock for the
// holds. Opened from the top bar by click and by tap and from Home; the
// Ruler's jaws dragged by a finger and read against the true scale; the
// card check changes the scale and is remembered; the Circle gauge; the Fold
// moving, then still (held, with a tap felt), its caliper gap 2 L sin(θ/2)
// and its card calibration; the Level lying (bubble, slope), standing (edge
// angle) and relative to a kept position; Photo: a generated photo of a card
// and a 100 mm bar on a table in perspective, picked (phone) and opened as a
// file (desktop), corners found, a corner dragged with the magnifier,
// Straighten, the bar measured by taps (within 1 %), a plate's outline and a
// washer's circle; Make sketch and Make box close the workspace into the
// design, and the sketch's lines or circle or the box body are measured there
// (then back to Measure from the top bar); Camera: a box and an outline;
// Keep, the tray, rename, delete, Use from openFor("length") and
// openFor("angle"); the value boxes' ruler end to end (the value chip's and a
// sketch dimension editor's: Measure opens on the Ruler, Use types the
// length into the box, applying it changes the model); the measurements and
// the calibration read back by a
// new controller (as after a restart). Every instrument at phone, iPhone
// Duo outer / inner / turned, iPad and desktop sizes, inside the safe area:
// pictures measure_<instrument>_<size>.png.

#include "app/AcceptanceRunner.h"
#include "core/Math.h"
#include "document/Document.h"
#include "document/ReferenceImage.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/SketchSession.h"
#include "measure/Homography.h"
#include "measure/ScreenScale.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLineF>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QEventPoint>
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
const Size kDuoOuter{QStringLiteral("duo_outer"), 466, 678, {0, 84, 34, 0}, true};
const Size kDuoInner{QStringLiteral("duo_inner"), 951, 669, {0, 84, 34, 0}, true};
const Size kDuoTurned{QStringLiteral("duo_inner_turned"), 669, 951, {82, 0, 34, 0}, true};
const Size kIpad{QStringLiteral("ipad"), 1180, 820, {24, 0, 20, 0}, true};
const Size kDesktop{QStringLiteral("desktop"), 1400, 900, {0, 0, 0, 0}, false};

// The table in the generated photo (mm): a card, a 100 mm bar, a 60 x 50 mm
// plate and a washer 30 mm across.
constexpr double kCardX = 30, kCardY = 30;
const Vec2 kBarA{40, 113}, kBarB{140, 113};
const std::array<Vec2, 4> kPlate{Vec2{150, 95}, Vec2{210, 95}, Vec2{210, 145}, Vec2{150, 145}};
const Vec2 kWasher{190, 42};
constexpr double kWasherRadius = 15;

struct State {
    std::unique_ptr<QTemporaryDir> dir;
    QString photo;
    measure::Homography tableToImage;
    std::array<QPointF, 4> cardCorners{};
    std::function<void(const ui::HingeReading&)> hinge;
    std::function<void(const ui::GravityReading&)> gravity;
    int haptics = 0;
    int picks = 0;
    int arStarts = 0;
    double clock = 0;
    // Between steps.
    double ppm = 0;
    double before = 0;
    QPointF dragFrom;
    QPointF corner;
    QPointF ringCenter;
    double ringR0 = 0;
    int count = 0;
    bool loupeSeen = false;
};
using StatePtr = std::shared_ptr<State>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

QString say(const QString& text) { return QStringLiteral("measure: ") + text; }

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

QRectF sceneRect(QQuickItem* i) { return i ? i->mapRectToScene(QRectF(0, 0, i->width(), i->height())) : QRectF(); }

QPointF center(AcceptanceRunner& r, const QString& name) { return sceneRect(item(r, name)).center(); }

bool press(AcceptanceRunner& r, const Size& size, const QString& name)
{
    return size.touch ? r.tapItem(name) : r.clickItem(name);
}

void applySize(AcceptanceRunner& r, const Size& s)
{
    r.app().setTouchMode(s.touch);
    r.resizeWindow(s.width, s.height);
    const bool none = s.safe[0] == 0 && s.safe[1] == 0 && s.safe[2] == 0 && s.safe[3] == 0;
    r.window()->setProperty("simulatedSafeArea", none ? QVariant() : QVariant(QVariantList{s.safe[0], s.safe[1], s.safe[2], s.safe[3]}));
}

// Every part of the workspace shown is inside the safe area; touch targets are 44 px.
void checkLayout(AcceptanceRunner& r, const Size& s, const QString& where)
{
    const QRectF safe = QRectF(s.safe[3], s.safe[0], r.window()->width() - s.safe[3] - s.safe[1],
                               r.window()->height() - s.safe[0] - s.safe[2])
                            .adjusted(-0.5, -0.5, 0.5, 0.5);
    QStringList outside;
    int seen = 0;
    for (const char* name : {"measureTitle", "measureTabs", "measureClose", "measureReadout", "measureTray", "measureInstrumentArea",
                             "rulerControls", "foldHold", "levelHold", "photoStraighten", "photoUndo", "cameraStart", "cardDone",
                             "cardPlus", "cardStretch"}) {
        QQuickItem* i = item(r, QString::fromLatin1(name));
        if (!i || !i->isVisible() || i->width() <= 0 || i->height() <= 0)
            continue;
        ++seen;
        const QRectF rect = sceneRect(i);
        if (!safe.contains(rect))
            outside << QStringLiteral("%1 %2,%3 %4x%5").arg(QString::fromLatin1(name)).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    }
    r.check(seen >= 4 && outside.isEmpty(), say(where + QStringLiteral(": inside the safe area")),
            outside.isEmpty() ? QStringLiteral("%1 parts").arg(seen) : outside.join(QStringLiteral("; ")));
    if (s.touch) {
        QQuickItem* close = item(r, QStringLiteral("measureClose"));
        QQuickItem* tabs = item(r, QStringLiteral("measureTabs"));
        r.check(close && close->width() >= 44 && close->height() >= 44 && tabs && tabs->height() >= 44,
                say(where + QStringLiteral(": Close and the instruments are 44 px targets")));
    }
}

// The Ruler takes the window's long side where there is room (docs/MEASURE.md):
// across a wide window, the readout and the kept measurements below it; down
// a tall one with room beside it, they and the Ruler's card at its right. Its
// scale is longer than the window's short side would give, and nothing of the
// workspace lies over it.
void checkRulerLongSide(AcceptanceRunner& r, const Size& s)
{
    QQuickItem* ruler = item(r, QStringLiteral("measureRuler"));
    QQuickItem* strip = item(r, QStringLiteral("rulerStrip"));
    if (!ruler || !strip) {
        r.check(false, say(s.name + QStringLiteral(": the ruler's scale")));
        return;
    }
    const double safeWidth = s.width - s.safe[1] - s.safe[3], safeHeight = s.height - s.safe[0] - s.safe[2];
    const bool tall = s.height > s.width;
    const QRectF stripRect = sceneRect(strip), rulerRect = sceneRect(ruler);
    const double length = tall ? stripRect.height() : stripRect.width();
    const bool longEnough = tall ? length > safeWidth - 40 : length >= 0.9 * safeWidth;
    r.check(ruler->property("vertical").toBool() == tall && longEnough, say(s.name + QStringLiteral(": the scale runs along the long side")),
            QStringLiteral("%1 pt %2, the safe area %3 x %4")
                .arg(AcceptanceRunner::num(length), tall ? QStringLiteral("down") : QStringLiteral("across"))
                .arg(safeWidth)
                .arg(safeHeight));
    QStringList over;
    for (const char* name : {"measureReadout", "measureTray"}) {
        QQuickItem* i = item(r, QString::fromLatin1(name));
        if (i && i->isVisible() && sceneRect(i).intersects(rulerRect.adjusted(1, 1, -1, -1)))
            over << QString::fromLatin1(name);
    }
    QQuickItem* controls = item(r, QStringLiteral("rulerControls"));
    if (controls && controls->isVisible() && sceneRect(controls).intersects(tall ? rulerRect.adjusted(1, 1, -1, -1) : stripRect))
        over << QStringLiteral("rulerControls");
    r.check(over.isEmpty(), say(s.name + QStringLiteral(": nothing lies over the ruler")), over.join(QStringLiteral(", ")));
}

// ---------------------------------------------------------------- the stand-ins

void pushHinge(const StatePtr& st, double degrees)
{
    if (!st->hinge)
        return;
    st->clock += 0.1;
    ui::HingeReading reading;
    reading.present = true;
    reading.angleRadians = degrees * kPi / 180.0;
    reading.status = ui::HingeStatus::PartiallyOpen;
    st->hinge(reading);
}

void pushGravity(const StatePtr& st, double x, double y, double z)
{
    if (!st->gravity)
        return;
    st->clock += 0.1;
    st->gravity({x, y, z});
}

ui::MeasureDevices standIns(const StatePtr& st)
{
    ui::MeasureDevices d;
    d.watchHinge = [st](QWindow*, std::function<void(const ui::HingeReading&)> handler) -> ui::StopWatching {
        st->hinge = std::move(handler);
        return [st] { st->hinge = nullptr; };
    };
    d.watchGravity = [st](QWindow*, std::function<void(const ui::GravityReading&)> handler) -> ui::StopWatching {
        st->gravity = std::move(handler);
        return [st] { st->gravity = nullptr; };
    };
    d.haptic = [st](int) { ++st->haptics; };
    d.cameraAvailable = [] { return true; };
    d.pickPhoto = [st](QWindow*, ui::PhotoOrigin, const QString& folder, const QRectF&, std::function<void(const ui::PhotoResult&)> finished) {
        ++st->picks;
        QDir().mkpath(folder);
        const QString out = folder + QStringLiteral("/picked_%1.jpg").arg(st->picks);
        ui::PhotoResult result;
        result.outcome = QImage(st->photo).save(out, "JPG", 95) ? ui::PhotoResult::Outcome::Picked : ui::PhotoResult::Outcome::Failed;
        result.file = out;
        QTimer::singleShot(0, QCoreApplication::instance(), [finished, result] { finished(result); });
    };
    d.detectRectangle = [st](const QString&) -> std::optional<ui::RectangleCorners> { return st->cardCorners; };
    d.arCapabilities = [] { return ui::ArCapabilities{true, true}; };
    d.startAr = [st](QWindow*, ui::ArMode mode, const QString&, std::function<void(const ui::ArResult&)> finished) {
        ++st->arStarts;
        ui::ArResult result;
        result.outcome = ui::ArResult::Outcome::Done;
        result.mode = mode;
        if (mode == ui::ArMode::Distance)
            result.distancesMm = {1234.5};
        if (mode == ui::ArMode::Outline)
            result.outlineMm = {QPointF(0, 0), QPointF(800, 0), QPointF(800, 500), QPointF(0, 500)};
        if (mode == ui::ArMode::Box) {
            result.widthMm = 600;
            result.depthMm = 400;
            result.heightMm = 750;
        }
        QTimer::singleShot(30, QCoreApplication::instance(), [finished, result] { finished(result); });
    };
    return d;
}

// ---------------------------------------------------------------- the photo

std::vector<Vec2> rectangle(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

std::vector<Vec2> circle(Vec2 c, double radius, int n = 96)
{
    std::vector<Vec2> pts;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * i / n;
        pts.push_back({c.x + radius * std::cos(a), c.y + radius * std::sin(a)});
    }
    return pts;
}

QPolygonF mapped(const measure::Homography& h, const std::vector<Vec2>& pts)
{
    QPolygonF poly;
    for (const Vec2& p : pts) {
        const Vec2 q = h.map(p);
        poly << QPointF(q.x, q.y);
    }
    return poly;
}

// A card, a bar, a plate and a washer on a wooden table, seen at an angle.
bool makePhoto(const StatePtr& st)
{
    const std::array<Vec2, 4> table{Vec2{0, 0}, Vec2{240, 0}, Vec2{240, 180}, Vec2{0, 180}};
    const std::array<Vec2, 4> image{Vec2{330, 300}, Vec2{1690, 250}, Vec2{1930, 1390}, Vec2{90, 1420}};
    const auto h = measure::Homography::fromQuads(table, image);
    if (!h)
        return false;
    st->tableToImage = *h;
    QImage img(2000, 1500, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient floor(0, 0, 0, 1500);
    floor.setColorAt(0, QColor(0x5A, 0x60, 0x68));
    floor.setColorAt(1, QColor(0x3A, 0x3F, 0x46));
    p.fillRect(img.rect(), floor);
    QLinearGradient wood(0, 200, 0, 1500);
    wood.setColorAt(0, QColor(0xD8, 0xB7, 0x8E));
    wood.setColorAt(1, QColor(0xB8, 0x8C, 0x60));
    p.setPen(Qt::NoPen);
    p.setBrush(wood);
    p.drawPolygon(mapped(*h, rectangle(-25, -25, 265, 205)));
    p.setPen(QPen(QColor(0x9C, 0x74, 0x4C, 70), 3));
    for (double y = -15; y < 205; y += 13)
        p.drawPolyline(mapped(*h, {{-25, y}, {80, y + 2}, {170, y - 1}, {265, y + 1}}));
    // The playing card (lying long side across), its shadow and red pips.
    const double cardW = measure::kPlayingCard.longMm, cardH = measure::kPlayingCard.shortMm;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawPolygon(mapped(*h, rectangle(kCardX + 1.2, kCardY + 1.8, kCardX + cardW + 1.2, kCardY + cardH + 1.8)));
    p.setBrush(QColor(0xFA, 0xFB, 0xFD));
    p.drawPolygon(mapped(*h, rectangle(kCardX, kCardY, kCardX + cardW, kCardY + cardH)));
    p.setBrush(QColor(0xC8, 0x2A, 0x2A));
    const std::array<std::pair<Vec2, double>, 3> pips{{{Vec2{kCardX + 8, kCardY + 9}, 4.0},
                                                       {Vec2{kCardX + cardW / 2, kCardY + cardH / 2}, 9.0},
                                                       {Vec2{kCardX + cardW - 8, kCardY + cardH - 9}, 4.0}}};
    for (const auto& [pip, size] : pips)
        p.drawPolygon(mapped(*h, {Vec2{pip.x, pip.y - size}, Vec2{pip.x + size * 0.7, pip.y}, Vec2{pip.x, pip.y + size}, Vec2{pip.x - size * 0.7, pip.y}}));
    // The bar, the plate and the washer.
    p.setBrush(QColor(0x2A, 0x2F, 0x36));
    p.drawPolygon(mapped(*h, rectangle(kBarA.x, kBarA.y - 3, kBarB.x, kBarB.y + 3)));
    p.setBrush(QColor(0x4F, 0x8F, 0x6A));
    p.drawPolygon(mapped(*h, {kPlate.begin(), kPlate.end()}));
    QPainterPath washer;
    washer.addPolygon(mapped(*h, circle(kWasher, kWasherRadius)));
    washer.closeSubpath();
    washer.addPolygon(mapped(*h, circle(kWasher, 7)));
    washer.closeSubpath();
    washer.setFillRule(Qt::OddEvenFill);
    p.setBrush(QColor(0x8C, 0x94, 0x9E));
    p.drawPath(washer);
    p.end();
    const std::array<Vec2, 4> card{Vec2{kCardX, kCardY}, Vec2{kCardX + cardW, kCardY}, Vec2{kCardX + cardW, kCardY + cardH},
                                   Vec2{kCardX, kCardY + cardH}};
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 q = h->map(card[i]);
        st->cardCorners[i] = QPointF(q.x, q.y);
    }
    st->photo = st->dir->filePath(QStringLiteral("table.png"));
    return img.save(st->photo);
}

// Where a point on the table appears in the straightened photo on screen.
std::optional<QPointF> onStraight(AcceptanceRunner& r, const StatePtr& st, Vec2 table)
{
    auto& m = measureOf(r);
    const QVariantList c = m.corners();
    if (c.size() != 4 || m.straightMmPerPixel() <= 0)
        return std::nullopt;
    std::array<Vec2, 4> quad;
    for (int i = 0; i < 4; ++i)
        quad[std::size_t(i)] = {c[i].toPointF().x(), c[i].toPointF().y()};
    const auto rect = measure::PlaneRectifier::create(quad, measure::cardReference());
    QQuickItem* image = item(r, QStringLiteral("photoStraightImage"));
    if (!rect || !image || !image->isVisible())
        return std::nullopt;
    const Vec2 plane = rect->toPlane(st->tableToImage.map(table));
    const double px = (plane.x - m.straightOriginMm().x()) / m.straightMmPerPixel();
    const double py = (plane.y - m.straightOriginMm().y()) / m.straightMmPerPixel();
    return image->mapToScene(QPointF(px * image->width() / m.straightSize().width(), py * image->height() / m.straightSize().height()));
}

bool tapTable(AcceptanceRunner& r, const StatePtr& st, Vec2 table)
{
    const auto p = onStraight(r, st, table);
    if (!p)
        return false;
    r.touchTap({*p});
    return true;
}

double readoutNumber(AcceptanceRunner& r) { return text(r, QStringLiteral("measureReadoutValue")).remove(QLatin1Char(',')).remove(QChar(0x00B0)).toDouble(); }

// A finger drag over several steps (Qt Quick takes one move a frame).
void fingerDrag(Steps& steps, AcceptanceRunner& r, const StatePtr& st, std::function<QPointF()> from, QPointF delta, int moves = 6,
                std::function<void()> midway = {})
{
    steps.push_back([&r, st, from] {
        st->dragFrom = from();
        r.touchPress(st->dragFrom);
    });
    for (int i = 1; i <= moves; ++i)
        steps.push_back([&r, st, delta, i, moves, midway] {
            r.touchMove(st->dragFrom + delta * (double(i) / moves));
            if (i == moves && midway)
                midway();
        });
    steps.push_back([&r, st, delta] { r.touchRelease(st->dragFrom + delta); });
}

// ---------------------------------------------------------------- the design

const interact::SketchSession* sketchOf(AcceptanceRunner& r) { return r.app().interaction().sketchSession(); }

// The open sketch's lines: how many, and the size of the box around them.
QSizeF linesExtent(AcceptanceRunner& r, int& count)
{
    count = 0;
    const auto* s = sketchOf(r);
    if (!s)
        return {};
    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (const auto& [id, line] : s->sketch().lines()) {
        ++count;
        for (const auto end : {line.start, line.end})
            if (const auto* p = s->sketch().point(end)) {
                minX = std::min(minX, p->position.x);
                maxX = std::max(maxX, p->position.x);
                minY = std::min(minY, p->position.y);
                maxY = std::max(maxY, p->position.y);
            }
    }
    return count ? QSizeF(maxX - minX, maxY - minY) : QSizeF();
}

// Where a point of the open sketch is on the screen.
QPointF onSketch(AcceptanceRunner& r, Vec2 local)
{
    const auto* s = sketchOf(r);
    if (!s)
        return {};
    const Vec2 p = r.app().interaction().camera().project(s->sketch().plane().toWorld(local));
    return {p.x, p.y};
}

// Back from the design to Measure: the sketch finished, a new document, the
// top bar's button (tapped or clicked).
void backToMeasure(Steps& steps, AcceptanceRunner& r, const QString& what, bool touch)
{
    steps.push_back([&r] {
        if (r.app().sketchMode())
            r.app().finishSketch();
        r.app().newDocument();
    });
    wait(steps, 3);
    steps.push_back([&r, what, touch] {
        r.check(touch ? r.tapItem(QStringLiteral("measureButton")) : r.clickItem(QStringLiteral("measureButton")),
                say(what + QStringLiteral(": back to Measure from the top bar")));
    });
    wait(steps, 2);
    steps.push_back([&r, what] { r.check(shown(r, QStringLiteral("measureScreen")), say(what + QStringLiteral(": Measure is open again"))); });
}

// The Duo unfolded: the Ruler's scale crosses the fold. Its crease is marked
// at the middle of the window's long side; a length across it asks for the
// phone to lie flat while the hinge is bent, and only then.
void addFoldCreaseSteps(Steps& steps, AcceptanceRunner& r, const StatePtr& st, const Size& size)
{
    const Size* s = &size;
    auto ruler = [&r] { return item(r, QStringLiteral("measureRuler")); };
    auto creaseMm = [ruler] {
        QQuickItem* i = ruler();
        return i ? i->property("creaseMm").toDouble() : 0.0;
    };
    auto setJaws = [ruler](double a, double b) {
        if (QQuickItem* i = ruler()) {
            i->setProperty("jawA", a);
            i->setProperty("jawB", b);
        }
    };
    auto notFlat = [ruler] {
        QQuickItem* i = ruler();
        return i && i->property("notFlat").toBool();
    };
    auto tag = [&r] { return prop(r, QStringLiteral("rulerFold"), "label").toString(); };
    auto caption = [&r] { return text(r, QStringLiteral("measureReadoutCaption")); };

    // Where the crease is; a length on one half while the hinge is bent
    // (92.5 degrees, as the loop left it).
    steps.push_back([&r, s, ruler, creaseMm, setJaws] {
        QQuickItem* i = ruler();
        const bool tall = s->height > s->width;
        const double crease = i ? i->property("crease").toDouble() : 0.0;
        const QPointF origin = i ? i->mapToScene(QPointF(0, 0)) : QPointF();
        const double at = (tall ? origin.y() : origin.x()) + crease;
        const double middle = (tall ? s->height : s->width) / 2.0;
        r.check(shown(r, QStringLiteral("rulerFold")) && std::abs(at - middle) < 0.5, say(s->name + QStringLiteral(": the fold's crease is marked")),
                QStringLiteral("at %1 pt, the window's middle %2").arg(AcceptanceRunner::num(at)).arg(middle));
        setJaws(0, creaseMm() - 6);
    });
    wait(steps, 1);
    steps.push_back([&r, s, ruler, creaseMm, setJaws, notFlat, tag, st] {
        QQuickItem* i = ruler();
        r.check(i && i->property("foldBent").toBool() && !notFlat() && tag() == QStringLiteral("Fold"),
                say(s->name + QStringLiteral(": bent, a length on one half needs nothing")));
        // Across the crease, the hinge at 150 degrees.
        setJaws(0, creaseMm() + 12);
        for (int k = 0; k < 25; ++k)
            pushHinge(st, 150);
    });
    wait(steps, 1);
    steps.push_back([&r, s, notFlat, tag, caption, st] {
        r.check(notFlat() && tag() == QStringLiteral("Open flat") && caption().contains(QStringLiteral("fold not flat")),
                say(s->name + QStringLiteral(": bent, a length across the fold asks to open the phone flat")), caption());
        // The warning is read whole, not cut short by the readout's width.
        r.check(!prop(r, QStringLiteral("measureReadoutCaption"), "truncated").toBool(),
                say(s->name + QStringLiteral(": the warning fits the readout")), caption());
        checkLayout(r, *s, s->name + QStringLiteral(" ruler, not flat"));
        r.screenshot(QStringLiteral("measure_ruler_%1_not_flat").arg(s->name));
        // Opened flat.
        for (int k = 0; k < 25; ++k)
            pushHinge(st, 179);
    });
    wait(steps, 1);
    steps.push_back([&r, s, setJaws, notFlat, tag, caption, st] {
        r.check(!notFlat() && tag() == QStringLiteral("Fold") && !caption().contains(QStringLiteral("fold")),
                say(s->name + QStringLiteral(": opened flat, the warning goes")), caption());
        // As the other instruments' pictures expect.
        setJaws(0, 40);
        pushHinge(st, 120);
        for (int k = 0; k < 25; ++k)
            pushHinge(st, 92.5);
    });
    wait(steps, 1);
}

// ---------------------------------------------------------------- the scenario

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<State>();
    Steps steps;

    steps.push_back([&r, st] {
        st->dir = std::make_unique<QTemporaryDir>();
        r.check(st->dir->isValid() && makePhoto(st), say("the test photo is drawn"), st->photo);
        auto& m = measureOf(r);
        m.setDevices(standIns(st));
        m.setClock([st] { return st->clock; });
        m.resetCalibration();
        m.resetCaliper();
        m.clearPhoto();
        m.clearArResult();
        for (const QVariant& e : m.measurements())
            m.removeMeasurement(e.toMap().value(QStringLiteral("id")).toString());
        if (QQuickItem* screen = item(r, QStringLiteral("measureScreen"))) {
            QMetaObject::invokeMethod(screen, "close");
            QMetaObject::invokeMethod(screen, "resetForTest");
        }
        r.check(m.measurements().isEmpty() && !m.scaleCalibrated(), say("a clean start"));
        r.check(m.pointsPerMm() > 1.5 && m.pointsPerMm() < 12, say("the desktop's scale is believable"), AcceptanceRunner::num(m.pointsPerMm()));
    });

    // ---- open and close: the top bar by click, Home
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureButton")), say("click the top bar's Measure button")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(shown(r, QStringLiteral("measureScreen")), say("the workspace opens"));
        r.check(measureOf(r).sensorsRunning() && bool(st->hinge) && bool(st->gravity), say("its sensors start"));
        r.check(text(r, QStringLiteral("measureTitle")) == QStringLiteral("Measure"), say("its title"));
        r.check(!shown(r, QStringLiteral("measureTab_fold")), say("no Fold before a hinge reports"));
        r.check(shown(r, QStringLiteral("measureTab_camera")), say("Camera where ARKit is"));
        r.check(r.clickItem(QStringLiteral("measureClose")), say("click Close"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(!shown(r, QStringLiteral("measureScreen")), say("the workspace closes"));
        r.check(!measureOf(r).sensorsRunning() && !st->hinge && !st->gravity, say("its sensors stop"));
        r.app().setHomeVisible(true);
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeMeasure")), say("Home: click Measure")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("measureScreen")) && shown(r, QStringLiteral("homeScreen")), say("Measure opens over Home"));
        r.check(r.clickItem(QStringLiteral("measureClose")), say("Home: close Measure"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("homeScreen")) && !shown(r, QStringLiteral("measureScreen")), say("back on Home"));
        r.app().setHomeVisible(false);
    });

    // ---- the phone, by touch
    steps.push_back([&r] { applySize(r, kPhone); });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("phone: tap the top bar's Measure button")); });
    wait(steps, 3);
    steps.push_back([&r, st] {
        r.check(shown(r, QStringLiteral("measureScreen")) && prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("ruler"),
                say("phone: the workspace opens on the Ruler"));
        checkLayout(r, kPhone, QStringLiteral("phone ruler"));
        auto* ruler = item(r, QStringLiteral("measureRuler"));
        r.check(ruler && ruler->property("vertical").toBool(), say("phone: the scale runs down the long side"));
        st->ppm = measureOf(r).pointsPerMm();
        st->before = ruler ? ruler->property("shownB").toDouble() : 0;
    });
    // The second jaw, dragged 96 pt along the scale.
    fingerDrag(steps, r, st, [&r] { return center(r, QStringLiteral("rulerJawBHandle")); }, QPointF(0, 96));
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto* ruler = item(r, QStringLiteral("measureRuler"));
        const double a = ruler->property("shownA").toDouble(), b = ruler->property("shownB").toDouble();
        r.check(std::abs(b - (st->before + 96 / st->ppm)) < 0.25, say("ruler: the jaw moved 96 pt, at the true scale"),
                QStringLiteral("%1 mm, expected %2").arg(b).arg(st->before + 96 / st->ppm));
        // The reading is the jaws' distance on the screen, in millimeters.
        const double onScreen = center(r, QStringLiteral("rulerJawBHandle")).y() - center(r, QStringLiteral("rulerJawAHandle")).y();
        r.check(std::abs(onScreen / st->ppm - (b - a)) < 0.1, say("ruler: the reading matches the jaws' distance on the screen"),
                QStringLiteral("%1 pt = %2 mm; reading %3").arg(onScreen).arg(onScreen / st->ppm).arg(b - a));
        r.check(text(r, QStringLiteral("measureReadoutValue")) == measureOf(r).formatNumber(QStringLiteral("length"), b - a),
                say("ruler: the readout shows it"), text(r, QStringLiteral("measureReadoutValue")));
        r.screenshot(QStringLiteral("measure_ruler_phone"));
    });
    // Keep it.
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureKeep")), say("ruler: tap Keep")); });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantList list = measureOf(r).measurements();
        r.check(list.size() == 1 && list[0].toMap().value(QStringLiteral("source")).toString() == QStringLiteral("Ruler"),
                say("Keep adds the length"), QString::number(list.size()));
        const QString name = list.isEmpty() ? QString() : list[0].toMap().value(QStringLiteral("name")).toString();
        r.check(text(r, QStringLiteral("measureToastText")) == QStringLiteral("Kept as ") + name + QStringLiteral(" · tap it to use it")
                    && shown(r, QStringLiteral("measureToast")),
                say("a toast says what it was kept as, and what next"), text(r, QStringLiteral("measureToastText")));
        r.check(shown(r, QStringLiteral("measureTray")) && shown(r, QStringLiteral("measureChip_0")), say("the tray shows it"));
        checkLayout(r, kPhone, QStringLiteral("phone ruler with the tray"));
    });

    // ---- check with a card
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("rulerCalibrate")), say("tap Check with a card")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(shown(r, QStringLiteral("cardCalibration")), say("card: the outline is shown"));
        st->before = prop(r, QStringLiteral("cardCalibration"), "spanPt").toDouble();
        r.check(std::abs(st->before - measure::kPlayingCard.longMm * st->ppm) < 0.01,
                say("card: a playing card's outline, drawn at the current scale"), AcceptanceRunner::num(st->before));
        r.check(text(r, QStringLiteral("cardInstructions")).contains(QStringLiteral("playing card"))
                    && text(r, QStringLiteral("cardSizeText")) == QStringLiteral("63.5 \u00D7 88.9 mm"),
                say("card: it says which card, and its size"), text(r, QStringLiteral("cardSizeText")));
        r.check(!item(r, QStringLiteral("cardKind")), say("card: a playing card, no other card offered"));
        QQuickItem* outline = item(r, QStringLiteral("cardOutline"));
        r.check(outline && std::abs(outline->height() - st->before) < 0.01, say("card: along the phone's long side"),
                outline ? AcceptanceRunner::num(outline->height()) : QString());
        checkLayout(r, kPhone, QStringLiteral("phone card"));
    });
    fingerDrag(steps, r, st, [&r] { return center(r, QStringLiteral("cardStretch")); }, QPointF(0, 18));
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("cardPlus")) && r.tapItem(QStringLiteral("cardPlus")), say("card: + twice")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const double span = prop(r, QStringLiteral("cardCalibration"), "spanPt").toDouble();
        r.check(std::abs(span - (st->before + 19)) < 0.3, say("card: stretched 18 pt and two half-point steps"),
                QStringLiteral("%1, expected %2").arg(span).arg(st->before + 19));
        st->before = span;
        r.screenshot(QStringLiteral("measure_card_phone"));
        r.check(r.tapItem(QStringLiteral("cardDone")), say("card: tap Done"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(!shown(r, QStringLiteral("cardCalibration")), say("card: closes"));
        r.check(m.scaleCalibrated() && std::abs(m.pointsPerMm() - st->before / measure::kPlayingCard.longMm) < 1e-6,
                say("card: the scale is the playing card's"), AcceptanceRunner::num(m.pointsPerMm()));
        QSettings settings;
        r.check(std::abs(settings.value(QStringLiteral("measure/scale/") + m.screenKey()).toDouble() - m.pointsPerMm()) < 1e-9,
                say("card: kept for this screen"), m.screenKey());
        m.updateScale();
        r.check(m.scaleCalibrated() && std::abs(m.pointsPerMm() - st->before / measure::kPlayingCard.longMm) < 1e-6, say("card: read back"));
        r.check(text(r, QStringLiteral("rulerScaleNote")) == QStringLiteral("Scale checked with a card"), say("card: the ruler says so"));
        r.check(!m.calibrate(10, measure::kPlayingCard.longMm), say("card: an implausible scale is refused"));
        st->ppm = m.pointsPerMm();
        // The jaws keep their millimeters: the reading still matches the screen.
        const double onScreen = center(r, QStringLiteral("rulerJawBHandle")).y() - center(r, QStringLiteral("rulerJawAHandle")).y();
        auto* ruler = item(r, QStringLiteral("measureRuler"));
        const double reading = ruler->property("shownB").toDouble() - ruler->property("shownA").toDouble();
        r.check(std::abs(onScreen / st->ppm - reading) < 0.1, say("ruler: at the new scale the jaws still read true"));
    });

    // ---- the circle gauge
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("rulerMode_circle")), say("tap Circle")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        QQuickItem* ring = item(r, QStringLiteral("rulerRing"));
        st->ringCenter = sceneRect(ring).center();
        st->ringR0 = ring ? ring->width() / 2 : 0;
        r.check(ring && ring->isVisible() && std::abs(ring->width() / st->ppm - 24) < 0.01, say("circle: a 24 mm ring at the true scale"));
    });
    fingerDrag(steps, r, st, [&r] { return center(r, QStringLiteral("rulerRingHandle")); }, QPointF(15, -15));
    wait(steps, 1);
    steps.push_back([&r, st] {
        const double d = prop(r, QStringLiteral("measureRuler"), "ringDiameter").toDouble();
        const double expected = 2 * (st->ringR0 + 15 * std::sqrt(2.0)) / st->ppm;
        r.check(std::abs(d - expected) < 0.3, say("circle: the ring follows the finger"), QStringLiteral("%1 mm, expected %2").arg(d).arg(expected));
        QQuickItem* ring = item(r, QStringLiteral("rulerRing"));
        r.check(ring && std::abs(ring->width() / st->ppm - d) < 0.01, say("circle: drawn at the true scale"));
        r.check(text(r, QStringLiteral("measureReadoutValue")) == measureOf(r).formatNumber(QStringLiteral("length"), d), say("circle: the diameter is read out"));
        r.screenshot(QStringLiteral("measure_ruler_circle_phone"));
    });
    steps.push_back([&r, st] {
        st->before = prop(r, QStringLiteral("measureRuler"), "ringDiameter").toDouble();
        r.check(r.tapItem(QStringLiteral("measureAction_circleSketch")), say("circle: Make sketch"));
    });
    wait(steps, 4);
    steps.push_back([&r, st] {
        const QStringList& made = measureOf(r).madeLog();
        r.check(!made.isEmpty() && made.last().startsWith(QStringLiteral("circle ")), say("circle: a circle sketch is asked for"),
                made.isEmpty() ? QString() : made.last());
        r.check(!shown(r, QStringLiteral("measureScreen")) && r.app().sketchMode(), say("circle: the workspace closes into the new sketch"));
        const auto* s = sketchOf(r);
        double diameter = 0;
        if (s)
            for (const auto& [id, c] : s->sketch().circles())
                diameter = 2 * c.radius;
        r.check(s && s->sketch().circles().size() == 1 && std::abs(diameter - st->before) < 1e-6,
                say("circle: the sketch's circle is the ring's diameter"), QStringLiteral("%1, ring %2").arg(diameter).arg(st->before));
        r.screenshot(QStringLiteral("measure_circle_sketch_phone"));
    });
    backToMeasure(steps, r, QStringLiteral("circle"), true);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("ruler"), say("circle: on the Ruler still"));
        r.check(r.tapItem(QStringLiteral("rulerMode_length")), say("back to Length"));
    });

    // ---- the fold
    steps.push_back([st] { pushHinge(st, 120); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(measureOf(r).hingeAvailable() && shown(r, QStringLiteral("measureTab_fold")), say("fold: the tab appears once the hinge reports"));
        r.check(r.tapItem(QStringLiteral("measureTab_fold")), say("fold: tap Fold"));
    });
    for (int i = 0; i < 8; ++i)
        steps.push_back([st, i] { pushHinge(st, 100 + 8 * i); });
    steps.push_back([&r, st] {
        r.check(!measureOf(r).foldHasHold() && !measureOf(r).foldSteady(), say("fold: moving, nothing held"));
        r.check(text(r, QStringLiteral("foldHoldTagText")) == QStringLiteral("LIVE"), say("fold: the number is marked live"));
        r.check(text(r, QStringLiteral("foldHoldHint")).contains(QStringLiteral("held after a second")), say("fold: a line says when it holds"),
                text(r, QStringLiteral("foldHoldHint")));
        st->count = st->haptics;
        // The Duo's hinge reports only changes: one reading at 90, then
        // nothing while time passes (the sensor tick feeds it again).
        pushHinge(st, 90);
        for (int i = 0; i < 11; ++i) {
            st->clock += 0.1;
            measureOf(r).tickSensors();
        }
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        QQuickItem* fill = item(r, QStringLiteral("foldHoldFill"));
        r.check(!m.foldHasHold() && m.foldProgress() > 0.2 && m.foldProgress() < 1 && fill && fill->width() > 10
                    && text(r, QStringLiteral("foldHoldText")) == QStringLiteral("Hold still\u2026"),
                say("fold: keeping still, the pill fills toward the hold"),
                QStringLiteral("progress %1, fill %2").arg(m.foldProgress()).arg(fill ? fill->width() : -1));
        r.screenshot(QStringLiteral("measure_fold_filling_phone"));
        for (int i = 0; i < 12; ++i) {
            st->clock += 0.1;
            m.tickSensors();
        }
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(m.foldHasHold() && std::abs(m.foldHeld() - 90) < 0.4, say("fold: still, the angle is held"), AcceptanceRunner::num(m.foldHeld()));
        r.check(st->haptics == st->count + 1, say("fold: a tap is felt once"), QString::number(st->haptics - st->count));
        r.check(text(r, QStringLiteral("foldHoldText")).startsWith(QStringLiteral("Held")), say("fold: the pill says held"), text(r, QStringLiteral("foldHoldText")));
        r.check(text(r, QStringLiteral("foldHoldTagText")) == QStringLiteral("HELD") && text(r, QStringLiteral("foldHoldHint")).startsWith(QStringLiteral("Held:")),
                say("fold: HELD over the number, and how to measure again"), text(r, QStringLiteral("foldHoldHint")));
        r.check(text(r, QStringLiteral("foldValue")) == QString::number(m.foldHeld(), 'f', 1), say("fold: the angle, huge"), text(r, QStringLiteral("foldValue")));
        r.check(std::abs(readoutNumber(r) - m.foldHeld()) < 0.06 && !shown(r, QStringLiteral("measureReadoutValue"))
                    && text(r, QStringLiteral("measureReadoutCaption")) == QStringLiteral("Angle · held"),
                say("fold: the readout keeps the held angle without repeating its number on a phone"), text(r, QStringLiteral("measureReadoutCaption")));
        checkLayout(r, kPhone, QStringLiteral("phone fold"));
        r.screenshot(QStringLiteral("measure_fold_phone"));
        // Measure again while the phone lies still: no hold at once.
        r.check(r.tapItem(QStringLiteral("foldMeasureAgain")), say("fold: Measure again"));
        for (int i = 0; i < 3; ++i) {
            st->clock += 0.1;
            m.tickSensors();
        }
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(!m.foldHasHold() && text(r, QStringLiteral("foldHoldTagText")) == QStringLiteral("LIVE"),
                say("fold: Measure again waits the whole second again, even lying still"), AcceptanceRunner::num(m.foldProgress()));
        st->count = st->haptics;
        for (int i = 0; i < 9; ++i) {
            st->clock += 0.1;
            m.tickSensors();
        }
        r.check(m.foldHasHold() && st->haptics == st->count + 1, say("fold: then held again, with a tap"));
        // Moving on: the hold stays until the next steady stretch.
        for (int i = 0; i < 3; ++i)
            pushHinge(st, 95 + 5 * i);
    });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("foldMode_gap")), say("fold: tap Gap")); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        const double arm = m.caliperArmMm();
        r.check(arm > 75 && arm < 90 && !m.caliperCalibrated(), say("caliper: the arm estimated from the inner display"), AcceptanceRunner::num(arm));
        const double expected = 2 * arm * std::sin(m.foldAngle() * kPi / 360);
        r.check(std::abs(m.caliperGapMm() - expected) < 1e-9, say("caliper: the gap is 2 L sin(angle / 2)"), AcceptanceRunner::num(m.caliperGapMm()));
        const double held = 2 * arm * std::sin(m.foldHeld() * kPi / 360);
        r.check(std::abs(m.caliperHeldGapMm() - held) < 1e-9 && text(r, QStringLiteral("foldValue")) == m.formatNumber(QStringLiteral("length"), held),
                say("caliper: the held gap is shown"), text(r, QStringLiteral("foldValue")));
        r.check(m.caliperMmPerDegree() > 0, say("caliper: how far a degree moves it"));
        r.screenshot(QStringLiteral("measure_fold_gap_phone"));
    });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("caliperCalibrate")), say("caliper: Calibrate with a card")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(text(r, QStringLiteral("caliperSide_short")).contains(QStringLiteral("63.5")), say("caliper: the playing card's short side"),
                text(r, QStringLiteral("caliperSide_short")));
    });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("caliperCalibrateDone")), say("caliper: Calibrate (the short side)")); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.caliperCalibrated() && std::abs(m.caliperHeldGapMm() - measure::kPlayingCard.shortMm) < 0.01,
                say("caliper: calibrated, the playing card's side reads 63.5 mm"), AcceptanceRunner::num(m.caliperHeldGapMm()));
        QSettings settings;
        r.check(std::abs(settings.value(QStringLiteral("measure/caliperArm")).toDouble() - m.caliperArmMm()) < 1e-9, say("caliper: the arm is kept"));
        m.resetCaliper();
        r.check(!m.caliperCalibrated(), say("caliper: reset"));
        r.check(r.tapItem(QStringLiteral("foldMode_angle")), say("fold: back to Angle"));
    });

    // ---- the level
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_level")), say("level: tap Level")); });
    steps.push_back([st] {
        const double a = 3 * kPi / 180;
        for (int i = 0; i < 25; ++i)
            pushGravity(st, std::sin(a), 0, -std::cos(a));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.gravityAvailable() && m.levelLying() && std::abs(m.levelSlope() - 3) < 1e-6, say("level: lying, a 3° slope"),
                AcceptanceRunner::num(m.levelSlope()));
        r.check(std::abs(m.bubbleX() + 0.3) < 1e-6 && std::abs(m.bubbleY()) < 1e-6, say("level: the bubble drifts to the high side"),
                AcceptanceRunner::num(m.bubbleX()));
        QQuickItem* vial = item(r, QStringLiteral("levelBubbleVial"));
        QQuickItem* bubble = item(r, QStringLiteral("levelBubble"));
        r.check(vial && bubble && sceneRect(bubble).center().x() < sceneRect(vial).center().x() - 10, say("level: drawn left of center"));
        r.check(m.levelHasHold() && text(r, QStringLiteral("levelValue")) == QStringLiteral("3.0"), say("level: held, 3.0°"),
                text(r, QStringLiteral("levelValue")));
        r.check(text(r, QStringLiteral("levelHoldTagText")) == QStringLiteral("HELD") && text(r, QStringLiteral("levelHoldHint")).startsWith(QStringLiteral("Held:")),
                say("level: HELD over the number, and how to measure again"), text(r, QStringLiteral("levelHoldHint")));
        r.screenshot(QStringLiteral("measure_level_phone"));
    });
    steps.push_back([st] {
        const double a = 10 * kPi / 180;
        for (int i = 0; i < 25; ++i)
            pushGravity(st, -std::sin(a), std::cos(a), 0);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(!m.levelLying() && std::abs(m.levelEdge() - 10) < 1e-6, say("level: standing, the edge 10° from level"), AcceptanceRunner::num(m.levelEdge()));
        r.check(shown(r, QStringLiteral("levelHorizon")) && std::abs(prop(r, QStringLiteral("levelEdgeBar"), "rotation").toDouble() + 10) < 2,
                say("level: the edge drawn tilted"));
        r.screenshot(QStringLiteral("measure_level_standing_phone"));
        r.check(r.tapItem(QStringLiteral("levelKeepReference")), say("level: Keep as reference"));
    });
    steps.push_back([st] {
        const double a = 45 * kPi / 180;
        for (int i = 0; i < 25; ++i)
            pushGravity(st, -std::sin(a), std::cos(a), 0);
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.hasLevelReference() && std::abs(m.relativeAngle() - 35) < 1e-6, say("level: 35° to the kept position"),
                AcceptanceRunner::num(m.relativeAngle()));
        r.check(std::abs(readoutNumber(r) - 35) < 0.06, say("level: the readout shows it"));
        r.screenshot(QStringLiteral("measure_level_reference_phone"));
        r.check(r.tapItem(QStringLiteral("levelClearReference")), say("level: Clear"));
    });
    steps.push_back([&r, st] {
        r.check(!measureOf(r).hasLevelReference(), say("level: the reference is gone"));
        for (int i = 0; i < 3; ++i)
            pushGravity(st, 0.05, 0.02, -1);
    });

    // ---- a photo (the picker)
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_photo")), say("photo: tap Photo")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoTake")) && shown(r, QStringLiteral("photoChoose")) && !shown(r, QStringLiteral("photoOpen")),
                say("photo: Take photo and Choose photo (a camera and the library)"));
        r.screenshot(QStringLiteral("measure_photo_start_phone"));
        r.check(r.tapItem(QStringLiteral("photoChoose")), say("photo: tap Choose photo"));
    });
    steps.push_back([&r, st] {
        r.check(measureOf(r).waitForPhoto(15000), say("photo: read"));
        r.check(st->picks == 1, say("photo: the picker was asked"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(prop(r, QStringLiteral("measurePhoto"), "stage").toString() == QStringLiteral("corners"), say("photo: the corners step"));
        r.check(m.cornersDetected() && m.photoSize() == QSizeF(2000, 1500), say("photo: the card was found"));
        bool same = m.corners().size() == 4;
        for (int i = 0; same && i < 4; ++i)
            same = QLineF(m.corners()[i].toPointF(), st->cardCorners[std::size_t(i)]).length() < 0.01;
        r.check(same, say("photo: the corners are the finder's"));
        checkLayout(r, kPhone, QStringLiteral("phone photo corners"));
        r.screenshot(QStringLiteral("measure_photo_corners_phone"));
        st->corner = m.corners()[1].toPointF();
        r.check(r.tapItem(QStringLiteral("photoRef_rectangle")), say("photo: a rectangle of typed sides"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("measureKeypad")) && shown(r, QStringLiteral("photoSideA")), say("rectangle: the keypad opens for its long side"));
        bool all = true;
        for (const char* key : {"1", "2", "0", "next", "8", "5"})
            all = r.tapItem(QStringLiteral("keypadKey_") + QString::fromLatin1(key)) && all;
        r.check(all, say("rectangle: 120, Next, 85 on the keypad"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(text(r, QStringLiteral("photoSideA")) == QStringLiteral("120") && text(r, QStringLiteral("photoSideB")) == QStringLiteral("85"),
                say("rectangle: the sides show what was typed"),
                text(r, QStringLiteral("photoSideA")) + QStringLiteral(" x ") + text(r, QStringLiteral("photoSideB")));
        r.check(m.referenceKind() == QStringLiteral("rectangle") && std::abs(m.referenceSideA() - 120) < 1e-9 && std::abs(m.referenceSideB() - 85) < 1e-9,
                say("rectangle: the reference is 120 x 85 mm"));
        checkLayout(r, kPhone, QStringLiteral("phone rectangle keypad"));
        r.screenshot(QStringLiteral("measure_photo_rectangle_phone"));
        r.check(r.tapItem(QStringLiteral("keypadKey_done")), say("rectangle: ✓"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("measureKeypad")), say("rectangle: the keypad closes"));
        r.check(r.tapItem(QStringLiteral("photoRef_card")), say("photo: back to the card"));
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(measureOf(r).referenceKind() == QStringLiteral("card"), say("photo: the card again")); });
    fingerDrag(steps, r, st, [&r] { return center(r, QStringLiteral("photoCorner_1")); }, QPointF(-14, 10), 6, [&r, st] {
        st->loupeSeen = shown(r, QStringLiteral("photoLoupe"));
        r.screenshot(QStringLiteral("measure_photo_loupe_phone"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        const QPointF moved = m.corners()[1].toPointF();
        r.check(moved.x() < st->corner.x() - 20 && moved.y() > st->corner.y() + 15, say("photo: a corner follows the finger"),
                QStringLiteral("%1,%2").arg(moved.x()).arg(moved.y()));
        r.check(st->loupeSeen && !shown(r, QStringLiteral("photoLoupe")), say("photo: the magnifier shows while dragging"));
        m.setCorner(1, st->corner.x(), st->corner.y());
        r.check(r.tapItem(QStringLiteral("photoStraighten")), say("photo: tap Straighten"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(20000), say("photo: straightened")); });
    wait(steps, 2);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(prop(r, QStringLiteral("measurePhoto"), "stage").toString() == QStringLiteral("measure"), say("photo: the measuring step"));
        const QVariantList ref = m.straightReference();
        const double longSide = ref.size() == 4 ? QLineF(ref[0].toPointF(), ref[1].toPointF()).length() * m.straightMmPerPixel() : 0;
        const double shortSide = ref.size() == 4 ? QLineF(ref[1].toPointF(), ref[2].toPointF()).length() * m.straightMmPerPixel() : 0;
        r.check(std::abs(longSide - measure::kPlayingCard.longMm) < 0.05 && std::abs(shortSide - measure::kPlayingCard.shortMm) < 0.05,
                say("photo: the playing card is square and true in the straightened view"), QStringLiteral("%1 x %2").arg(longSide).arg(shortSide));
        checkLayout(r, kPhone, QStringLiteral("phone photo straightened"));
    });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarA) , say("photo (phone): tap the bar's end")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarB), say("photo (phone): tap its other end")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        r.check(std::abs(readoutNumber(r) - 100) < 2, say("photo (phone): about 100 mm"), text(r, QStringLiteral("measureReadoutValue")));
        r.screenshot(QStringLiteral("measure_photo_distance_phone"));
        st->ringCenter = center(r, QStringLiteral("photoStraightView"));
        st->count = int(prop(r, QStringLiteral("measurePhoto"), "points").toList().size());
        r.touchFrame({st->ringCenter - QPointF(40, 0), st->ringCenter + QPointF(40, 0)}, QEventPoint::State::Pressed);
    });
    for (int i = 1; i <= 6; ++i)
        steps.push_back([&r, st, i] {
            const QPointF d(40 + 16 * i, 0);
            r.touchFrame({st->ringCenter - d, st->ringCenter + d}, QEventPoint::State::Updated);
        });
    steps.push_back([&r, st] {
        const QPointF d(40 + 16 * 6, 0);
        r.touchFrame({st->ringCenter - d, st->ringCenter + d}, QEventPoint::State::Released);
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const double zoom = prop(r, QStringLiteral("photoStraightView"), "zoom").toDouble();
        r.check(zoom > 2, say("photo: two fingers zoom in"), AcceptanceRunner::num(zoom));
        r.check(int(prop(r, QStringLiteral("measurePhoto"), "points").toList().size()) == st->count, say("photo: a pinch adds no point"));
        r.check(shown(r, QStringLiteral("photoStraightImageFit")), say("photo: Fit is offered"));
        r.screenshot(QStringLiteral("measure_photo_zoomed_phone"));
        r.check(r.tapItem(QStringLiteral("photoStraightImageFit")), say("photo: tap Fit"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(std::abs(prop(r, QStringLiteral("photoStraightView"), "zoom").toDouble() - 1) < 1e-9, say("photo: the whole photo again"));
    });

    // ---- a value for a value box
    steps.push_back([&r] {
        r.check(r.tapItem(QStringLiteral("measureClose")), say("close"));
        QMetaObject::invokeMethod(item(r, QStringLiteral("measureScreen")), "openFor", Q_ARG(QVariant, QStringLiteral("length")));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        auto* ruler = item(r, QStringLiteral("measureRuler"));
        const double length = ruler->property("shownB").toDouble() - ruler->property("shownA").toDouble();
        const QString expected = QStringLiteral("Use ") + measureOf(r).formatValue(QStringLiteral("length"), length);
        r.check(shown(r, QStringLiteral("measureScreen")) && prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("ruler"),
                say("openFor length: the Ruler"));
        r.check(text(r, QStringLiteral("measureUse")) == expected, say("openFor length: the button reads Use <length>"), text(r, QStringLiteral("measureUse")));
        r.screenshot(QStringLiteral("measure_use_phone"));
        r.check(r.tapItem(QStringLiteral("measureUse")), say("openFor length: tap Use"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto* ruler = item(r, QStringLiteral("measureRuler"));
        const double length = ruler->property("shownB").toDouble() - ruler->property("shownA").toDouble();
        r.check(prop(r, QStringLiteral("measureScreen"), "lastUsedText").toString() == measureOf(r).valueText(QStringLiteral("length"), length),
                say("openFor length: useValue gets the value text"), prop(r, QStringLiteral("measureScreen"), "lastUsedText").toString());
        r.check(!shown(r, QStringLiteral("measureScreen")), say("openFor length: the workspace closes"));
        QMetaObject::invokeMethod(item(r, QStringLiteral("measureScreen")), "openFor", Q_ARG(QVariant, QStringLiteral("angle")));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("fold"), say("openFor angle: the Fold"));
        r.check(r.tapItem(QStringLiteral("measureUse")), say("openFor angle: tap Use"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measureScreen"), "lastUsedText").toString() == measureOf(r).valueText(QStringLiteral("angle"), measureOf(r).foldHeld()),
                say("openFor angle: the held angle"), prop(r, QStringLiteral("measureScreen"), "lastUsedText").toString());
        QMetaObject::invokeMethod(item(r, QStringLiteral("measureScreen")), "openFor", Q_ARG(QVariant, QStringLiteral("length")));
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureChip_0")), say("tray: tap the kept length")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("measureChipCard")) && shown(r, QStringLiteral("measureChipUse")), say("tray: its card offers Use"));
        r.screenshot(QStringLiteral("measure_chip_phone"));
        r.check(r.tapItem(QStringLiteral("measureChipUse")), say("tray: tap Use"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantMap kept = measureOf(r).measurements().value(0).toMap();
        r.check(prop(r, QStringLiteral("measureScreen"), "lastUsedText").toString() == kept.value(QStringLiteral("valueText")).toString()
                    && !shown(r, QStringLiteral("measureScreen")),
                say("tray: Use types the kept value"), prop(r, QStringLiteral("measureScreen"), "lastUsedText").toString());
    });

    // ---- the desktop: a photo opened as a file, measured exactly
    steps.push_back([&r, st] {
        applySize(r, kDesktop);
        auto& m = measureOf(r);
        m.clearPhoto();
        ui::MeasureDevices d = standIns(st);
        d.pickPhoto = nullptr; // a desktop: a file dialog
        d.cameraAvailable = nullptr;
        m.setDevices(d);
        r.app().setNextFileChoice(QUrl::fromLocalFile(st->photo));
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureButton")), say("desktop: click Measure")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureTab_photo")), say("desktop: click Photo")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoOpen")) && !shown(r, QStringLiteral("photoTake")) && !shown(r, QStringLiteral("photoChoose")),
                say("desktop: Open image… (no picker, no camera)"));
        r.screenshot(QStringLiteral("measure_photo_start_desktop"));
        r.check(r.clickItem(QStringLiteral("photoOpen")), say("desktop: click Open image…"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(15000) && measureOf(r).cornersDetected(), say("desktop: the photo opens, the card found")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.screenshot(QStringLiteral("measure_photo_corners_desktop"));
        r.check(r.clickItem(QStringLiteral("photoStraighten")), say("desktop: click Straighten"));
    });
    steps.push_back([&r] {
        r.check(measureOf(r).waitForPhoto(20000), say("desktop: straightened"));
        r.app().setTouchMode(true);
    });
    wait(steps, 2);
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarA), say("photo: tap one end of the bar")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarB), say("photo: tap the other end")); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto* photo = item(r, QStringLiteral("measurePhoto"));
        const QVariantList pts = photo->property("points").toList();
        const double d = pts.size() == 2 ? measureOf(r).distanceMm(pts[0].toPointF(), pts[1].toPointF()) : 0;
        r.check(std::abs(d - 100) < 1, say("photo: the 100 mm bar measures within 1 %"), AcceptanceRunner::num(d));
        r.check(text(r, QStringLiteral("measureReadoutValue")) == measureOf(r).formatNumber(QStringLiteral("length"), d), say("photo: read out"));
        r.check(shown(r, QStringLiteral("photoLabel_0")), say("photo: the distance labelled on the photo"));
        r.screenshot(QStringLiteral("measure_photo_distance_desktop"));
        r.check(r.tapItem(QStringLiteral("measureKeepWide")), say("photo: Keep"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantMap kept = measureOf(r).measurements().value(0).toMap();
        r.check(kept.value(QStringLiteral("source")).toString() == QStringLiteral("Photo") && std::abs(kept.value(QStringLiteral("value")).toDouble() - 100) < 1,
                say("photo: kept"));
        r.check(r.tapItem(QStringLiteral("photoMode_outline")), say("photo: Outline"));
    });
    for (int i = 0; i < 4; ++i)
        steps.push_back([&r, st, i] { r.check(tapTable(r, st, kPlate[std::size_t(i)]), say("outline: tap a corner of the plate")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kPlate[0]), say("outline: tap the first corner again")); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto* photo = item(r, QStringLiteral("measurePhoto"));
        r.check(photo->property("closed").toBool() && photo->property("points").toList().size() == 4, say("outline: closed on the first point"));
        const QVariantMap report = measureOf(r).outlineReport(photo->property("points").toList(), true);
        const QVariantList sides = report.value(QStringLiteral("sides")).toList();
        const double area = report.value(QStringLiteral("area")).toDouble();
        r.check(std::abs(area - 3000) < 60, say("outline: the plate's area, 3000 mm² within 2 %"), AcceptanceRunner::num(area));
        r.check(sides.size() == 4 && std::abs(sides[0].toDouble() - 60) < 0.6 && std::abs(sides[1].toDouble() - 50) < 0.5,
                say("outline: its sides, 60 and 50 mm within 1 %"),
                sides.size() == 4 ? QStringLiteral("%1, %2").arg(sides[0].toDouble()).arg(sides[1].toDouble()) : QString());
        r.check(text(r, QStringLiteral("measureReadoutCaption")).startsWith(QStringLiteral("Area")), say("outline: the area read out"));
        r.screenshot(QStringLiteral("measure_photo_outline_desktop"));
        r.check(r.tapItem(QStringLiteral("measureAction_outlineSketch")), say("outline: Make sketch"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(!m.madeLog().isEmpty() && m.madeLog().last() == QStringLiteral("outline 4"), say("outline: an outline sketch is asked for"),
                m.madeLog().isEmpty() ? QString() : m.madeLog().last());
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const QVariant& v : m.lastOutlinePoints()) {
            minX = std::min(minX, v.toPointF().x());
            maxX = std::max(maxX, v.toPointF().x());
            minY = std::min(minY, v.toPointF().y());
            maxY = std::max(maxY, v.toPointF().y());
        }
        r.check(std::abs(maxX - minX - 60) < 0.8 && std::abs(maxY - minY - 50) < 0.8 && std::abs(minX + maxX) < 1e-6 && std::abs(minY + maxY) < 1e-6,
                say("outline: at real size, centered on the origin"), QStringLiteral("%1 x %2").arg(maxX - minX).arg(maxY - minY));
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("measureScreen")) && r.app().sketchMode(), say("outline: the workspace closes into the new sketch"));
        int lines = 0;
        const QSizeF size = linesExtent(r, lines);
        r.check(lines == 4 && std::abs(size.width() - 60) < 0.8 && std::abs(size.height() - 50) < 0.8,
                say("outline: the sketch has the plate's four sides, 60 x 50 mm"),
                QStringLiteral("%1 lines, %2 x %3").arg(lines).arg(size.width()).arg(size.height()));
        r.screenshot(QStringLiteral("measure_outline_sketch_desktop"));
    });
    backToMeasure(steps, r, QStringLiteral("outline"), true);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("photo")
                    && prop(r, QStringLiteral("measurePhoto"), "stage").toString() == QStringLiteral("measure"),
                say("outline: the straightened photo is still there"));
        r.check(r.tapItem(QStringLiteral("photoMode_circle")), say("photo: Circle"));
    });
    for (const double a : {20.0, 140.0, 260.0})
        steps.push_back([&r, st, a] {
            const Vec2 p{kWasher.x + kWasherRadius * std::cos(a * kPi / 180), kWasher.y + kWasherRadius * std::sin(a * kPi / 180)};
            r.check(tapTable(r, st, p), say("circle: tap the washer's rim"));
        });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantList pts = prop(r, QStringLiteral("measurePhoto"), "points").toList();
        const QVariantMap c = pts.size() == 3 ? measureOf(r).circleFrom3(pts[0].toPointF(), pts[1].toPointF(), pts[2].toPointF()) : QVariantMap();
        const double d = c.value(QStringLiteral("diameter")).toDouble();
        r.check(std::abs(d - 30) < 0.45, say("circle: the washer, 30 mm across within 1.5 %"), AcceptanceRunner::num(d));
        r.check(shown(r, QStringLiteral("photoCircle")), say("circle: drawn on the photo"));
        r.screenshot(QStringLiteral("measure_photo_circle_desktop"));
        const bool designTakesImages = r.app().metaObject()->indexOfMethod("addReferenceImage(QString,double,QString)") >= 0;
        r.check(shown(r, QStringLiteral("measureAction_placeImage")) == designTakesImages,
                say(designTakesImages ? "Place in design is offered" : "no Place in design until the design takes images"));
    });
    steps.push_back([&r, st] {
        // The wheel zooms at the pointer: the photo's point there stays put.
        st->dragFrom = onStraight(r, st, kWasher).value_or(QPointF());
        r.wheel(st->dragFrom, 3);
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const double zoom = prop(r, QStringLiteral("photoStraightView"), "zoom").toDouble();
        const QPointF now = onStraight(r, st, kWasher).value_or(QPointF());
        r.check(zoom > 1.2 && QLineF(now, st->dragFrom).length() < 1.5, say("photo: the wheel zooms at the pointer"),
                QStringLiteral("%1x, moved %2").arg(zoom).arg(QLineF(now, st->dragFrom).length()));
        r.screenshot(QStringLiteral("measure_photo_zoomed_desktop"));
        r.check(r.tapItem(QStringLiteral("photoStraightImageFit")), say("photo: Fit"));
    });
    wait(steps, 1);
    // Place in design: the straightened photo as a reference photo on the ground, at its size.
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        st->before = m.straightSize().width() * m.straightMmPerPixel();
        r.check(r.app().document().references().empty(), say("place: no reference photo yet"));
        r.check(r.tapItem(QStringLiteral("measureAction_placeImage")), say("place: tap Place in design"));
    });
    wait(steps, 3);
    steps.push_back([&r, st] {
        r.check(!shown(r, QStringLiteral("measureScreen")), say("place: the workspace closes into the design"));
        const auto& refs = r.app().document().references();
        r.check(refs.size() == 1, say("place: one reference photo"), QString::number(refs.size()));
        if (refs.size() != 1)
            return;
        const doc::ReferenceImage& photo = *refs.front();
        r.check(std::abs(photo.width - st->before) < 0.1, say("place: as wide as the straightened view (its pixels x mm per pixel)"),
                QStringLiteral("%1 mm, expected %2").arg(photo.width).arg(st->before));
        r.check(photo.plane == doc::ReferencePlane::XY, say("place: on the ground"));
        const QString id = QString::fromStdString(photo.id().toString());
        bool listed = false;
        for (const QVariant& row : r.app().history())
            listed = listed || row.toMap().value(QStringLiteral("id")).toString() == id;
        r.check(listed, say("place: the Model panel lists it"));
        r.screenshot(QStringLiteral("measure_place_desktop"));
        r.app().undo();
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.app().document().references().empty(), say("place: undo removes it")); });
    backToMeasure(steps, r, QStringLiteral("place"), true);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measurePhoto"), "stage").toString() == QStringLiteral("measure"), say("place: the straightened photo is still there"));
    });

    // ---- the camera (AR)
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_camera")), say("camera: tap Camera")); });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("cameraMode_box")), say("camera: Box")); });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("cameraStart")), say("camera: Start")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        const QVariantMap result = measureOf(r).arResult();
        r.check(st->arStarts == 1 && result.value(QStringLiteral("outcome")).toString() == QStringLiteral("done")
                    && result.value(QStringLiteral("mode")).toString() == QStringLiteral("box"),
                say("camera: the box comes back"));
        r.check(std::abs(readoutNumber(r) - 600) < 0.05 && text(r, QStringLiteral("measureReadoutCaption")).startsWith(QStringLiteral("Width")),
                say("camera: its width read out"), text(r, QStringLiteral("measureReadoutValue")));
        r.check(shown(r, QStringLiteral("measureReadoutItem_2")), say("camera: depth and height beside it"));
        r.screenshot(QStringLiteral("measure_camera_box_desktop"));
        r.check(r.tapItem(QStringLiteral("measureAction_box")), say("camera: Make box"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const QStringList& made = measureOf(r).madeLog();
        r.check(!made.isEmpty() && made.last() == QStringLiteral("box 600.0 400.0 750.0"), say("camera: a box of that size is asked for"),
                made.isEmpty() ? QString() : made.last());
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("measureScreen")), say("camera: the workspace closes into the design"));
        const double volume = r.app().bodyCount() == 1 ? r.bodyVolume() : 0;
        r.check(std::abs(volume - 600.0 * 400 * 750) < 1, say("camera: a 600 x 400 x 750 mm box body"), AcceptanceRunner::num(volume));
        if (r.app().bodyCount() == 1) {
            const auto bb = geom::boundingBox(r.body(0).shape());
            r.check(std::abs(bb.max.x - bb.min.x - 600) < 1e-6 && std::abs(bb.max.y - bb.min.y - 400) < 1e-6
                        && std::abs(bb.max.z - bb.min.z - 750) < 1e-6,
                    say("camera: width along X, depth along Y, height along Z"));
        }
        r.screenshot(QStringLiteral("measure_box_body_desktop"));
    });
    backToMeasure(steps, r, QStringLiteral("camera box"), true);
    steps.push_back([&r] {
        r.check(prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("camera"), say("camera: on Camera still"));
        r.check(r.tapItem(QStringLiteral("cameraMode_outline")), say("camera: Outline"));
    });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("cameraStart")), say("camera: Start")); });
    wait(steps, 2);
    steps.push_back([&r] {
        const QVariantMap result = measureOf(r).arResult();
        r.check(result.value(QStringLiteral("mode")).toString() == QStringLiteral("outline") && std::abs(result.value(QStringLiteral("area")).toDouble() - 400000) < 1,
                say("camera: the outline comes back, 800 x 500 mm"));
        r.screenshot(QStringLiteral("measure_camera_outline_desktop"));
        r.check(r.tapItem(QStringLiteral("measureAction_outlineSketch")), say("camera: Make sketch"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(!m.madeLog().isEmpty() && m.madeLog().last() == QStringLiteral("outline 4") && m.lastOutlinePoints().size() == 4
                    && std::abs(m.lastOutlinePoints()[0].toPointF().x() + 400) < 1e-6,
                say("camera: an outline sketch at real size is asked for"));
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("measureScreen")) && r.app().sketchMode(), say("camera: the workspace closes into the new sketch"));
        int lines = 0;
        const QSizeF size = linesExtent(r, lines);
        r.check(lines == 4 && std::abs(size.width() - 800) < 1e-6 && std::abs(size.height() - 500) < 1e-6,
                say("camera: the sketch's outline is 800 x 500 mm"), QStringLiteral("%1 lines, %2 x %3").arg(lines).arg(size.width()).arg(size.height()));
    });
    backToMeasure(steps, r, QStringLiteral("camera outline"), true);

    // ---- the value boxes' ruler, end to end (the mouse)
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("measureClose")), say("ruler button: close Measure"));
        r.app().setTouchMode(false);
        r.app().newDocument();
    });
    wait(steps, 2);
    steps.push_back([&r] { r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b")); });
    wait(steps, 2);
    steps.push_back([&r] { r.app().fitAll(); });
    wait(steps, 4);
    steps.push_back([&r] { r.click(r.screenPoint(0, 0, 20)); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), say("ruler button: the top face offers push/pull"), r.app().operationTitle());
        st->before = r.app().bodyCount() == 1 ? r.bodyVolume() : 0;
        r.check(r.clickItem(QStringLiteral("valueChipMeasure")), say("ruler button: click the value chip's ruler"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("measureScreen")) && prop(r, QStringLiteral("measureScreen"), "forKind").toString() == QStringLiteral("length")
                    && prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("ruler"),
                say("ruler button: Measure opens on the Ruler, for a length"));
        if (QQuickItem* ruler = item(r, QStringLiteral("measureRuler"))) {
            ruler->setProperty("jawA", 0.0);
            ruler->setProperty("jawB", 35.0);
        }
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(text(r, QStringLiteral("measureUse")) == QStringLiteral("Use ") + measureOf(r).formatValue(QStringLiteral("length"), 35),
                say("ruler button: Use 35.0 mm"), text(r, QStringLiteral("measureUse")));
        r.screenshot(QStringLiteral("measure_for_value_desktop"));
        r.check(r.clickItem(QStringLiteral("measureUse")), say("ruler button: click Use"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(!shown(r, QStringLiteral("measureScreen")), say("ruler button: back in the design"));
        r.check(text(r, QStringLiteral("valueChipField")).contains(QStringLiteral("35")), say("ruler button: the value box shows 35"),
                text(r, QStringLiteral("valueChipField")));
        r.check(std::abs(r.bodyVolume() - st->before) < 1e-6, say("ruler button: not applied before ✓"));
        r.check(r.clickItem(QStringLiteral("valueChipApply")), say("ruler button: ✓"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(std::abs(r.bodyVolume() - 20.0 * 20 * 35) < 1e-3, say("ruler button: the cube is 35 mm tall"), AcceptanceRunner::num(r.bodyVolume()));
        r.app().newDocument();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(r.app().sketchMode() && r.clickItem(QStringLiteral("tool_rectangle")), say("dimension ruler: a sketch, Rectangle"));
        r.click(onSketch(r, {0, 0}));
        r.mouseMove(onSketch(r, {14, 7}));
        r.type(QStringLiteral("40"));
        r.key(Qt::Key_Tab);
        r.type(QStringLiteral("20"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        st->count = -1;
        if (const auto* s = sketchOf(r))
            for (const auto& [id, c] : s->sketch().constraints())
                if (c.isDimension() && std::abs(std::abs(c.value) - 40) < 1e-9)
                    st->count = int(id);
        r.check(st->count >= 0, say("dimension ruler: the 40 mm width"));
        r.check(r.clickItem(QStringLiteral("dimensionLabel_%1_tap").arg(st->count)), say("dimension ruler: click it"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("dimensionEditor")), say("dimension ruler: its editor"));
        r.check(r.clickItem(QStringLiteral("dimensionMeasure")), say("dimension ruler: click its ruler"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("measureScreen")) && prop(r, QStringLiteral("measureScreen"), "current").toString() == QStringLiteral("ruler"),
                say("dimension ruler: Measure opens on the Ruler"));
        if (QQuickItem* ruler = item(r, QStringLiteral("measureRuler"))) {
            ruler->setProperty("jawA", 0.0);
            ruler->setProperty("jawB", 30.0);
        }
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureUse")), say("dimension ruler: click Use 30.0 mm")); });
    wait(steps, 2);
    steps.push_back([&r] {
        QQuickItem* editor = item(r, QStringLiteral("dimensionEditor"));
        const QString value = editor ? editor->property("text").toString() : QString();
        r.check(!shown(r, QStringLiteral("measureScreen")) && editor && editor->isVisible() && value == measureOf(r).valueText(QStringLiteral("length"), 30),
                say("dimension ruler: the editor shows the measured 30 mm"), value);
        if (editor)
            editor->forceActiveFocus();
        r.key(Qt::Key_Return);
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const auto* s = sketchOf(r);
        const auto* c = s ? s->sketch().constraint(sketch::EntityId(st->count)) : nullptr;
        r.check(c && std::abs(std::abs(c->value) - 30) < 1e-9, say("dimension ruler: Enter applies 30 mm"), c ? AcceptanceRunner::num(c->value) : QString());
        r.app().finishSketch();
        r.app().newDocument();
        r.app().setTouchMode(true);
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("back to Measure")); });
    wait(steps, 2);

    // ---- rename and delete a kept measurement (the list at the side)
    steps.push_back([&r, st] {
        st->count = int(measureOf(r).measurements().size());
        r.check(r.tapItem(QStringLiteral("measureChip_0")), say("tray: tap the newest"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("measureChipCard")) && !shown(r, QStringLiteral("measureChipUse")), say("tray: its card (no Use: no value box asked)"));
        r.check(r.tapItem(QStringLiteral("measureChipRename")), say("tray: Rename"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        QQuickItem* field = item(r, QStringLiteral("measureRenameField"));
        r.check(field && field->isVisible(), say("tray: the name's field"));
        if (field)
            field->setProperty("text", QStringLiteral("Shelf depth"));
    });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureRenameSave")), say("tray: Save")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(measureOf(r).measurements().value(0).toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Shelf depth")
                    && text(r, QStringLiteral("measureChipName")) == QStringLiteral("Shelf depth"),
                say("tray: renamed"));
        r.screenshot(QStringLiteral("measure_chip_desktop"));
        // A kept length into the open project, under a name value boxes take.
        r.check(shown(r, QStringLiteral("measureChipToProject")), say("tray: its card offers Add to project"));
        r.check(r.tapItem(QStringLiteral("measureChipToProject")), say("tray: Add to project"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantList kept = r.app().property("projectMeasurements").toList();
        const QVariantMap first = kept.isEmpty() ? QVariantMap() : kept.front().toMap();
        const double expected = measureOf(r).measurements().value(0).toMap().value(QStringLiteral("value")).toDouble();
        r.check(kept.size() == 1 && first.value(QStringLiteral("name")).toString() == QStringLiteral("shelf_depth")
                    && std::abs(first.value(QStringLiteral("value")).toDouble() - expected) < 1e-9,
                say("tray: in the project as shelf_depth, at the kept value"),
                kept.isEmpty() ? QStringLiteral("none") : first.value(QStringLiteral("name")).toString());
        // The card stays and says how to use it; the list marks it.
        r.check(shown(r, QStringLiteral("measureChipCard")) && !shown(r, QStringLiteral("measureChipToProject"))
                    && text(r, QStringLiteral("measureChipDesignText")).startsWith(QStringLiteral("In the project as shelf_depth. Type shelf_depth")),
                say("tray: the card says its name in the project and how to type it"), text(r, QStringLiteral("measureChipDesignText")));
        // A chip in a row gets a check; a row in the side list, the name.
        const QString marked = text(r, QStringLiteral("measureChipInProject_0"));
        r.check(shown(r, QStringLiteral("measureChipInProject_0")) && (marked.isEmpty() || marked == QStringLiteral("shelf_depth")),
                say("tray: the kept measurement is marked as in the project"), marked);
        r.screenshot(QStringLiteral("measure_chip_in_project_desktop"));
        r.check(r.clickItem(QStringLiteral("measureChipClose")), say("tray: close the card"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.app().undo(); // the project as it was
        r.check(r.app().property("projectMeasurements").toList().isEmpty() && !shown(r, QStringLiteral("measureChipInProject_0")),
                say("tray: one undo takes it out of the project (and off the list)"));
        // How measurements reach the design.
        r.check(r.clickItem(QStringLiteral("measureTrayHelp")), say("guide: the ? beside the kept measurements"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        QQuickItem* guide = item(r, QStringLiteral("measureGuide"));
        const int waiting = guide ? int(guide->property("notInProject").toList().size()) : -1;
        r.check(shown(r, QStringLiteral("measureGuide")) && shown(r, QStringLiteral("measureGuideAddAll")) && waiting > 0,
                say("guide: three ways, and Add all to the project"), QString::number(waiting));
        r.screenshot(QStringLiteral("measure_guide_desktop"));
        st->count = waiting;
        r.check(r.clickItem(QStringLiteral("measureGuideAddAll")), say("guide: Add all"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const int inProject = int(r.app().property("projectMeasurements").toList().size());
        r.check(inProject == st->count && !shown(r, QStringLiteral("measureGuide")) && shown(r, QStringLiteral("measureChipInProject_0")),
                say("guide: every kept length and angle is in the project"), QStringLiteral("%1 of %2").arg(inProject).arg(st->count));
        for (int i = 0; i < inProject; ++i)
            r.app().undo();
        r.check(r.app().property("projectMeasurements").toList().isEmpty(), say("guide: undone, one step each"));
        r.check(r.tapItem(QStringLiteral("measureChip_0")), say("tray: the kept length again"));
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureChipDelete")), say("tray: Delete")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        r.check(int(measureOf(r).measurements().size()) == st->count - 1 && !shown(r, QStringLiteral("measureChipCard")), say("tray: deleted"));
        // Something to show in every picture: a kept angle and area.
        measureOf(r).addMeasurement(QStringLiteral("angle"), 92.5, QStringLiteral("Fold"));
        measureOf(r).addMeasurement(QStringLiteral("area"), 3000, QStringLiteral("Photo"), QStringLiteral("Plate"));
        measureOf(r).setDevices(standIns(st)); // the picker and the camera back
    });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureClose")), say("close")); });

    // ---- every instrument at every size
    for (const Size* s : {&kPhone, &kDuoOuter, &kDuoInner, &kDuoTurned, &kIpad, &kDesktop}) {
        const bool unfolded = s == &kDuoInner || s == &kDuoTurned;
        steps.push_back([&r, s, unfolded] {
            applySize(r, *s);
            // The Duo's inner display, whole (not Split View): its fold's crease.
            measureOf(r).setAssumeFoldingDisplay(unfolded);
        });
        wait(steps, 3);
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("measureButton")), say(s->name + QStringLiteral(": open"))); });
        wait(steps, 2);
        steps.push_back([st] {
            pushHinge(st, 120);
            for (int i = 0; i < 25; ++i)
                pushHinge(st, 92.5);
            for (int i = 0; i < 25; ++i)
                pushGravity(st, 0.04, -0.03, -1);
        });
        for (const char* instrument : {"ruler", "fold", "level", "photo", "camera"}) {
            const QString key = QString::fromLatin1(instrument);
            steps.push_back([&r, s, key] { r.check(press(r, *s, QStringLiteral("measureTab_") + key), say(s->name + QStringLiteral(": ") + key)); });
            wait(steps, 2);
            steps.push_back([&r, s, key] {
                checkLayout(r, *s, s->name + QLatin1Char(' ') + key);
                if (key == QStringLiteral("ruler") && s != &kPhone && s != &kDuoOuter)
                    checkRulerLongSide(r, *s);
                r.screenshot(QStringLiteral("measure_%1_%2").arg(key, s->name));
            });
            if (key == QStringLiteral("ruler") && unfolded)
                addFoldCreaseSteps(steps, r, st, *s);
        }
        steps.push_back([&r, s] {
            if (s->name == QStringLiteral("ipad")) {
                r.check(press(r, *s, QStringLiteral("measureTab_ruler")) , say("ipad: the ruler"));
            }
        });
        if (s == &kIpad) {
            wait(steps, 1);
            steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("rulerCalibrate")), say("ipad: Check with a card")); });
            wait(steps, 2);
            steps.push_back([&r, s] {
                checkLayout(r, *s, QStringLiteral("ipad card"));
                r.screenshot(QStringLiteral("measure_card_ipad"));
                r.check(r.tapItem(QStringLiteral("cardCancel")), say("ipad: Cancel"));
            });
        }
        steps.push_back([&r, s] {
            r.check(press(r, *s, QStringLiteral("measureClose")), say(s->name + QStringLiteral(": close")));
            measureOf(r).setAssumeFoldingDisplay(false);
        });
        wait(steps, 1);
    }

    // ---- after a restart: a new controller reads what was kept
    steps.push_back([&r] {
        auto& m = measureOf(r);
        auto* fresh = new ui::MeasureController(&r.app());
        QStringList now, read;
        for (const QVariant& e : m.measurements())
            now << e.toMap().value(QStringLiteral("name")).toString() + QLatin1Char('=') + e.toMap().value(QStringLiteral("text")).toString();
        for (const QVariant& e : fresh->measurements())
            read << e.toMap().value(QStringLiteral("name")).toString() + QLatin1Char('=') + e.toMap().value(QStringLiteral("text")).toString();
        r.check(!now.isEmpty() && now == read, say("restart: the measurements are read back"), read.join(QStringLiteral(", ")));
        r.check(fresh->scaleCalibrated() && std::abs(fresh->pointsPerMm() - m.pointsPerMm()) < 1e-9, say("restart: the card's scale is read back"));
        delete fresh;
    });

    // ---- clean up for the next scenarios
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        m.resetCalibration();
        m.resetCaliper();
        m.clearPhoto();
        m.clearArResult();
        for (const QVariant& e : m.measurements())
            m.removeMeasurement(e.toMap().value(QStringLiteral("id")).toString());
        m.setDevices(ui::platformMeasureDevices());
        m.setClock(nullptr);
        st->dir.reset();
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measure"), 112, steps});

} // namespace
} // namespace os::app
