// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Photo's guides, dimensions and views (docs/MEASURE.md, "Photo"): two
// generated photos (a table seen from above with a playing card, a bar and a
// plate; a part seen from the front beside a card), picked and straightened.
// The millimetre rulers along the straightened view's top and left read true
// against the photo's own millimetres, at fit and zoomed in; the grid (10 mm,
// finer when zoomed); a distance placed by a finger shows its angle and the
// guides while placed, snaps straight within 1.5 degrees (green, "straight"),
// a slanted one keeps its angle, and with Snap off a nearly straight one
// stays as placed. Several dimensions stay on one photo (A, B, C), listed
// under it: selected, renamed, deleted, Keep all (named "Top Width"). An
// outline and an origin point; + View keeps the Top view and starts the
// Front one; the views strip switches between them (their dimensions come
// back); Place views puts both photos at true size on the ground and the
// Front plane with their origin points at the origin, and the outlines as
// sketches on those planes (measured there); undo takes it all back. The
// Photo workspace at every size, nothing over anything else.

#include "app/AcceptanceRunner.h"
#include "core/Math.h"
#include "document/Document.h"
#include "document/ReferenceImage.h"
#include "measure/Homography.h"
#include "measure/ScreenScale.h"
#include "sketch/Sketch.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QLineF>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QEventPoint>
#include <QtGui/QImage>
#include <QtGui/QPainter>
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

// A photographed table (mm): where its corners show in the photo, the card's
// corner, and the dark parts on it.
struct Scene {
    std::array<Vec2, 4> image;
    Vec2 card;
    std::vector<std::vector<Vec2>> parts;
};

std::vector<Vec2> rectangle(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

// The top: a bar 100 mm long and a 60 x 50 mm plate (as the measure scenario's).
const Vec2 kBarA{40, 113}, kBarB{140, 113};
const std::array<Vec2, 4> kPlate{Vec2{150, 95}, Vec2{210, 95}, Vec2{210, 145}, Vec2{150, 145}};
Scene topScene()
{
    return {{Vec2{330, 300}, Vec2{1690, 250}, Vec2{1930, 1390}, Vec2{90, 1420}},
            {30, 30},
            {rectangle(kBarA.x, kBarA.y - 3, kBarB.x, kBarB.y + 3), {kPlate.begin(), kPlate.end()}}};
}
// The front: the part's side, 80 x 40 mm, its bottom-left corner the origin.
const std::array<Vec2, 4> kSide{Vec2{90, 70}, Vec2{170, 70}, Vec2{170, 110}, Vec2{90, 110}};
Scene frontScene()
{
    return {{Vec2{250, 260}, Vec2{1750, 330}, Vec2{1850, 1400}, Vec2{150, 1330}}, {20, 30}, {{kSide.begin(), kSide.end()}}};
}

struct State {
    std::unique_ptr<QTemporaryDir> dir;
    std::array<QString, 2> photos;
    std::array<measure::Homography, 2> homographies;
    std::array<std::array<QPointF, 4>, 2> cards{};
    QString photo;
    measure::Homography tableToImage;
    std::array<QPointF, 4> cardCorners{};
    int picks = 0;
    // Between steps.
    QPointF dragFrom;
    QPointF center;
    double gridStepBefore = 0;
    // Each view's straightened size, scale and two table points in its pixels.
    struct ViewPx {
        QSizeF size;
        double mmPerPixel = 0;
        QPointF a, b;
    };
    std::array<ViewPx, 2> views;
};
using StatePtr = std::shared_ptr<State>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

QString say(const QString& text) { return QStringLiteral("measurephotoviews: ") + text; }
ui::MeasureController& measureOf(AcceptanceRunner& r) { return *r.app().measure(); }
QQuickItem* item(AcceptanceRunner& r, const QString& name) { return r.findItem(name); }
bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = item(r, name);
    return i && i->isVisible();
}
QVariant prop(AcceptanceRunner& r, const QString& name, const char* property)
{
    QQuickItem* i = item(r, name);
    return i ? i->property(property) : QVariant();
}
QString text(AcceptanceRunner& r, const QString& name) { return prop(r, name, "text").toString(); }
QRectF sceneRect(QQuickItem* i) { return i ? i->mapRectToScene(QRectF(0, 0, i->width(), i->height())) : QRectF(); }
QVariant photoProp(AcceptanceRunner& r, const char* property) { return prop(r, QStringLiteral("measurePhoto"), property); }
QVariantList dims(AcceptanceRunner& r) { return photoProp(r, "dims").toList(); }
QVariantMap dim(AcceptanceRunner& r, int i) { return dims(r).value(i).toMap(); }
QString n(double v) { return AcceptanceRunner::num(v); }

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

// ---------------------------------------------------------------- the photos

QPolygonF mapped(const measure::Homography& h, const std::vector<Vec2>& pts)
{
    QPolygonF poly;
    for (const Vec2& p : pts) {
        const Vec2 q = h.map(p);
        poly << QPointF(q.x, q.y);
    }
    return poly;
}

bool makePhoto(const StatePtr& st, int index, const Scene& scene)
{
    const std::array<Vec2, 4> table{Vec2{0, 0}, Vec2{240, 0}, Vec2{240, 180}, Vec2{0, 180}};
    const auto h = measure::Homography::fromQuads(table, scene.image);
    if (!h)
        return false;
    st->homographies[std::size_t(index)] = *h;
    QImage img(2000, 1500, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(img.rect(), QColor(0x4A, 0x50, 0x58));
    QLinearGradient wood(0, 200, 0, 1500);
    wood.setColorAt(0, QColor(0xD8, 0xB7, 0x8E));
    wood.setColorAt(1, QColor(0xB8, 0x8C, 0x60));
    p.setPen(Qt::NoPen);
    p.setBrush(wood);
    p.drawPolygon(mapped(*h, rectangle(-25, -25, 265, 205)));
    const double cardW = measure::kPlayingCard.longMm, cardH = measure::kPlayingCard.shortMm;
    const Vec2 c = scene.card;
    p.setBrush(QColor(0xFA, 0xFB, 0xFD));
    p.drawPolygon(mapped(*h, rectangle(c.x, c.y, c.x + cardW, c.y + cardH)));
    p.setBrush(QColor(0xC8, 0x2A, 0x2A));
    p.drawPolygon(mapped(*h, {Vec2{c.x + cardW / 2, c.y + cardH / 2 - 9}, Vec2{c.x + cardW / 2 + 6, c.y + cardH / 2},
                              Vec2{c.x + cardW / 2, c.y + cardH / 2 + 9}, Vec2{c.x + cardW / 2 - 6, c.y + cardH / 2}}));
    p.setBrush(QColor(0x3C, 0x45, 0x50));
    for (const auto& part : scene.parts)
        p.drawPolygon(mapped(*h, part));
    p.end();
    const std::array<Vec2, 4> card{Vec2{c.x, c.y}, Vec2{c.x + cardW, c.y}, Vec2{c.x + cardW, c.y + cardH}, Vec2{c.x, c.y + cardH}};
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 q = h->map(card[i]);
        st->cards[std::size_t(index)][i] = QPointF(q.x, q.y);
    }
    st->photos[std::size_t(index)] = st->dir->filePath(QStringLiteral("view_%1.png").arg(index));
    return img.save(st->photos[std::size_t(index)]);
}

// The picker and the rectangle finder give photo `index` from now on.
void usePhoto(const StatePtr& st, int index)
{
    st->photo = st->photos[std::size_t(index)];
    st->tableToImage = st->homographies[std::size_t(index)];
    st->cardCorners = st->cards[std::size_t(index)];
}

ui::MeasureDevices standIns(const StatePtr& st)
{
    ui::MeasureDevices d;
    d.cameraAvailable = [] { return true; };
    d.pickPhoto = [st](QWindow*, ui::PhotoOrigin, const QString& folder, const QRectF&, std::function<void(const ui::PhotoResult&)> finished) {
        ++st->picks;
        QDir().mkpath(folder);
        const QString out = folder + QStringLiteral("/views_picked_%1.jpg").arg(st->picks);
        ui::PhotoResult result;
        result.outcome = QImage(st->photo).save(out, "JPG", 95) ? ui::PhotoResult::Outcome::Picked : ui::PhotoResult::Outcome::Failed;
        result.file = out;
        QTimer::singleShot(0, QCoreApplication::instance(), [finished, result] { finished(result); });
    };
    d.detectRectangle = [st](const QString&) -> std::optional<ui::RectangleCorners> { return st->cardCorners; };
    return d;
}

// Where a point on the table is in the straightened photo (its pixels).
std::optional<QPointF> straightPx(AcceptanceRunner& r, const StatePtr& st, Vec2 table)
{
    auto& m = measureOf(r);
    const QVariantList c = m.corners();
    if (c.size() != 4 || m.straightMmPerPixel() <= 0)
        return std::nullopt;
    std::array<Vec2, 4> quad;
    for (int i = 0; i < 4; ++i)
        quad[std::size_t(i)] = {c[i].toPointF().x(), c[i].toPointF().y()};
    const auto rect = measure::PlaneRectifier::create(quad, measure::cardReference());
    if (!rect)
        return std::nullopt;
    const Vec2 plane = rect->toPlane(st->tableToImage.map(table));
    return QPointF((plane.x - m.straightOriginMm().x()) / m.straightMmPerPixel(), (plane.y - m.straightOriginMm().y()) / m.straightMmPerPixel());
}

// ... and on the screen.
std::optional<QPointF> onStraight(AcceptanceRunner& r, const StatePtr& st, Vec2 table)
{
    const auto px = straightPx(r, st, table);
    QQuickItem* image = item(r, QStringLiteral("photoStraightImage"));
    auto& m = measureOf(r);
    if (!px || !image || !image->isVisible())
        return std::nullopt;
    return image->mapToScene(QPointF(px->x() * image->width() / m.straightSize().width(), px->y() * image->height() / m.straightSize().height()));
}

bool tapTable(AcceptanceRunner& r, const StatePtr& st, Vec2 table)
{
    const auto p = onStraight(r, st, table);
    if (!p)
        return false;
    r.touchTap({*p});
    return true;
}

// ---------------------------------------------------------------- checks

// The rulers read true: the top one's mark 100 mm right of its zero lies
// on the photo's point 100 mm right of `zero` (a table point), the left
// one's mark 40 mm down on the point 40 mm down; their scale is the view's.
void checkRulers(AcceptanceRunner& r, const StatePtr& st, const QString& where, Vec2 zero)
{
    QQuickItem* top = item(r, QStringLiteral("photoRulerTop"));
    QQuickItem* left = item(r, QStringLiteral("photoRulerLeft"));
    auto& m = measureOf(r);
    const double ppm = photoProp(r, "pxPerMm").toDouble();
    const double fit = prop(r, QStringLiteral("photoStraightView"), "fit").toDouble();
    r.check(top && left && top->isVisible() && left->isVisible() && ppm > 0 && std::abs(ppm * m.straightMmPerPixel() - fit) < 1e-9,
            say(where + QStringLiteral(": millimetre rulers along the top and the left, at the view's scale")), n(ppm) + QStringLiteral(" px/mm"));
    if (!top || !left)
        return;
    const auto right = onStraight(r, st, {zero.x + 100, zero.y});
    const QPointF mark = top->mapToScene(QPointF(top->property("zero").toDouble() + 100 * ppm, 0));
    r.check(right && std::abs(mark.x() - right->x()) < 1.0, say(where + QStringLiteral(": the top ruler's 100 mm is 100 mm along the photo")),
            right ? n(mark.x() - right->x()) + QStringLiteral(" px off") : QString());
    const auto down = onStraight(r, st, {zero.x, zero.y + 40});
    const QPointF markL = left->mapToScene(QPointF(0, left->property("zero").toDouble() + 40 * ppm));
    r.check(down && std::abs(markL.y() - down->y()) < 1.0, say(where + QStringLiteral(": the left ruler's -40 mm is 40 mm down the photo")),
            down ? n(markL.y() - down->y()) + QStringLiteral(" px off") : QString());
    const int label = photoProp(r, "rulerLabelStep").toInt(), minor = photoProp(r, "rulerMinorStep").toInt();
    r.check(label * ppm >= 46 && minor * ppm >= 6 && label % minor == 0, say(where + QStringLiteral(": the rulers' numbers have room, their ticks too")),
            QStringLiteral("every %1 mm, ticks %2 mm").arg(label).arg(minor));
}

int expectedGridStep(double ppm) { return ppm >= 8 ? 1 : 5 * ppm >= 8 ? 5 : 10 * ppm >= 6 ? 10 : 50; }

// Every part of the workspace inside the safe area (and Close and the tabs
// 44 px targets), Photo's parts inside its area and none over another.
void checkPhotoLayout(AcceptanceRunner& r, const Size& s, const QString& where)
{
    const QRectF safe = QRectF(s.safe[3], s.safe[0], r.window()->width() - s.safe[3] - s.safe[1], r.window()->height() - s.safe[0] - s.safe[2])
                            .adjusted(-0.5, -0.5, 0.5, 0.5);
    QStringList outside;
    for (const char* name : {"measureTitle", "measureTabs", "measureClose", "measureReadout", "measureTray", "measureInstrumentArea"}) {
        QQuickItem* i = item(r, QString::fromLatin1(name));
        if (i && i->isVisible() && i->width() > 0 && !safe.contains(sceneRect(i)))
            outside << QString::fromLatin1(name);
    }
    r.check(outside.isEmpty(), say(where + QStringLiteral(": inside the safe area")), outside.join(QStringLiteral(", ")));
    const QRectF area = sceneRect(item(r, QStringLiteral("measureInstrumentArea"))).adjusted(-0.5, -0.5, 0.5, 0.5);
    QList<QPair<QString, QRectF>> parts;
    QStringList out;
    for (const char* name : {"photoModes", "photoViews", "photoBack", "photoViewBox", "photoMeasureHint", "photoDims", "photoClose", "photoUndo",
                             "photoClear"}) {
        QQuickItem* i = item(r, QString::fromLatin1(name));
        if (!i || !i->isVisible() || i->width() <= 0 || i->height() <= 0)
            continue;
        const QRectF rect = sceneRect(i);
        parts.append({QString::fromLatin1(name), rect});
        if (!area.contains(rect))
            out << QString::fromLatin1(name);
    }
    QStringList over;
    for (int a = 0; a < parts.size(); ++a)
        for (int b = a + 1; b < parts.size(); ++b)
            if (parts[a].second.adjusted(0.5, 0.5, -0.5, -0.5).intersects(parts[b].second.adjusted(0.5, 0.5, -0.5, -0.5)))
                over << parts[a].first + QLatin1Char('/') + parts[b].first;
    for (const char* name : {"measureReadout", "measureTray"}) {
        QQuickItem* i = item(r, QString::fromLatin1(name));
        if (i && i->isVisible() && sceneRect(i).adjusted(0.5, 0.5, -0.5, -0.5).intersects(area.adjusted(1, 1, -1, -1)))
            over << QString::fromLatin1(name) + QStringLiteral("/photo");
    }
    const QRectF view = sceneRect(item(r, QStringLiteral("photoStraightView"))).adjusted(-0.5, -0.5, 0.5, 0.5);
    const QRectF box = sceneRect(item(r, QStringLiteral("photoViewBox"))).adjusted(-0.5, -0.5, 0.5, 0.5);
    if (!view.contains(sceneRect(item(r, QStringLiteral("photoViewTools")))))
        out << QStringLiteral("photoViewTools");
    for (const char* name : {"photoRulerTop", "photoRulerLeft"})
        if (!box.contains(sceneRect(item(r, QString::fromLatin1(name)))))
            out << QString::fromLatin1(name);
    r.check(parts.size() >= 5 && out.isEmpty() && over.isEmpty(), say(where + QStringLiteral(": Photo's parts in its area, none over another")),
            QStringLiteral("%1 parts; outside: %2; over: %3").arg(parts.size()).arg(out.join(QStringLiteral(", ")), over.join(QStringLiteral(", "))));
    if (s.touch) {
        QStringList tiny;
        for (const char* name : {"photoUndo", "photoAddView", "photoDim_0", "photoViewChip_0", "photoGrid", "photoSnap", "photoOriginButton"}) {
            QQuickItem* i = item(r, QString::fromLatin1(name));
            if (i && i->isVisible() && i->height() < 44)
                tiny << QString::fromLatin1(name);
        }
        r.check(tiny.isEmpty(), say(where + QStringLiteral(": Photo's buttons are 44 px targets")), tiny.join(QStringLiteral(", ")));
    }
}

// The open sketch's or a document sketch's points (not its origin): the box around them.
QRectF pointsBox(const sketch::Sketch& s)
{
    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (const auto& [id, p] : s.points()) {
        if (id == sketch::kOriginId)
            continue;
        minX = std::min(minX, p.position.x);
        maxX = std::max(maxX, p.position.x);
        minY = std::min(minY, p.position.y);
        maxY = std::max(maxY, p.position.y);
    }
    return minX <= maxX ? QRectF(QPointF(minX, minY), QPointF(maxX, maxY)) : QRectF();
}

bool closeTo(QPointF a, QPointF b, double tolerance) { return QLineF(a, b).length() <= tolerance; }

// Where a point of a placed photo (its pixels) is on its plane (mm).
QPointF onPlane(const doc::ReferenceImage& ref, const State::ViewPx& view, QPointF px)
{
    return {ref.center.x + (px.x() - view.size.width() / 2) * view.mmPerPixel, ref.center.y + (view.size.height() / 2 - px.y()) * view.mmPerPixel};
}

// ---------------------------------------------------------------- the scenario

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<State>();
    Steps steps;

    steps.push_back([&r, st] {
        st->dir = std::make_unique<QTemporaryDir>();
        r.check(st->dir->isValid() && makePhoto(st, 0, topScene()) && makePhoto(st, 1, frontScene()), say("the two test photos are drawn"));
        usePhoto(st, 0);
        auto& m = measureOf(r);
        m.setDevices(standIns(st));
        m.clearPhoto();
        m.clearPhotoViews();
        for (const QVariant& e : m.measurements())
            m.removeMeasurement(e.toMap().value(QStringLiteral("id")).toString());
        if (QQuickItem* screen = item(r, QStringLiteral("measureScreen"))) {
            QMetaObject::invokeMethod(screen, "close");
            QMetaObject::invokeMethod(screen, "resetForTest");
        }
        r.app().newDocument();
        applySize(r, kDesktop);
        r.app().setTouchMode(true);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("open Measure")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_photo")), say("tap Photo")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("photoStartViews")), say("no views strip before there is a set"));
        r.check(r.tapItem(QStringLiteral("photoChoose")), say("tap Choose photo"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(15000) && measureOf(r).cornersDetected(), say("the top photo is read, the card found")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoStraighten")), say("tap Straighten")); });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(20000), say("straightened")); });
    wait(steps, 3);

    // ---- the rulers and the grid
    steps.push_back([&r, st] {
        r.check(photoProp(r, "stage").toString() == QStringLiteral("measure"), say("the measuring step"));
        checkRulers(r, st, QStringLiteral("fit"), {30, 30});
        r.check(!shown(r, QStringLiteral("photoGridLines")) && !photoProp(r, "showGrid").toBool(), say("no grid until asked"));
        r.check(r.tapItem(QStringLiteral("photoGrid")), say("tap Grid"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const double ppm = photoProp(r, "pxPerMm").toDouble();
        const int step = prop(r, QStringLiteral("photoGridLines"), "step").toInt();
        const QVariantList lines = prop(r, QStringLiteral("photoGridLines"), "lines").toList();
        r.check(shown(r, QStringLiteral("photoGridLines")) && step == expectedGridStep(ppm) && step <= 10,
                say("the grid: 10 mm lines (finer only when zoomed in)"), QStringLiteral("%1 mm at %2 px/mm").arg(step).arg(ppm));
        r.check(lines.size() == 2 && lines[1].toList().size() > 10, say("the grid has its 10 mm lines"),
                QString::number(lines.value(1).toList().size()));
        st->gridStepBefore = step;
        r.screenshot(QStringLiteral("measurephotoviews_grid_desktop"));
        st->center = sceneRect(item(r, QStringLiteral("photoStraightView"))).center();
        r.touchFrame({st->center - QPointF(40, 0), st->center + QPointF(40, 0)}, QEventPoint::State::Pressed);
    });
    for (int i = 1; i <= 6; ++i)
        steps.push_back([&r, st, i] {
            const QPointF d(40 + 30 * i, 0);
            r.touchFrame({st->center - d, st->center + d}, QEventPoint::State::Updated);
        });
    steps.push_back([&r, st] {
        const QPointF d(40 + 30 * 6, 0);
        r.touchFrame({st->center - d, st->center + d}, QEventPoint::State::Released);
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const double zoom = prop(r, QStringLiteral("photoStraightView"), "zoom").toDouble();
        const double ppm = photoProp(r, "pxPerMm").toDouble();
        const int step = prop(r, QStringLiteral("photoGridLines"), "step").toInt();
        r.check(zoom > 3, say("two fingers zoom in"), n(zoom));
        checkRulers(r, st, QStringLiteral("zoomed"), {30, 30});
        r.check(step == expectedGridStep(ppm) && step < st->gridStepBefore, say("zoomed in, the grid is finer"),
                QStringLiteral("%1 mm (was %2) at %3 px/mm").arg(step).arg(st->gridStepBefore).arg(ppm));
        r.check(dims(r).isEmpty(), say("a pinch adds no point"));
        r.screenshot(QStringLiteral("measurephotoviews_grid_zoomed_desktop"));
        r.check(r.tapItem(QStringLiteral("photoStraightImageFit")), say("tap Fit"));
    });
    wait(steps, 1);

    // ---- a distance placed by a finger: its angle, the guides, snapped straight
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarA), say("distance: tap the bar's end")); });
    steps.push_back([&r, st] {
        st->dragFrom = onStraight(r, st, {140, 121}).value_or(QPointF());
        r.touchPress(st->dragFrom);
    });
    steps.push_back([&r] {
        const double angle = prop(r, QStringLiteral("photoPlaceAngle"), "angle").toDouble();
        const double expected = std::atan(8.0 / 100.0) * 180 / kPi;
        r.check(shown(r, QStringLiteral("photoPlaceAngle")) && shown(r, QStringLiteral("photoPlaceGuides")),
                say("distance: while placed, its angle and the guides through the first end show"));
        r.check(std::abs(angle - expected) < 0.2 && !prop(r, QStringLiteral("photoPlaceAngle"), "straight").toBool(),
                say("distance: 8 mm off over 100 mm reads about 4.6 degrees, not straight"), n(angle));
        r.check(text(r, QStringLiteral("photoMeasureHint")).contains(QStringLiteral("°")), say("distance: the hint says the angle too"),
                text(r, QStringLiteral("photoMeasureHint")));
    });
    for (int i = 1; i <= 4; ++i)
        steps.push_back([&r, st, i] {
            const QPointF to = onStraight(r, st, {140, 114.2}).value_or(QPointF());
            r.touchMove(st->dragFrom + (to - st->dragFrom) * (i / 4.0));
        });
    steps.push_back([&r] {
        const double angle = prop(r, QStringLiteral("photoPlaceAngle"), "angle").toDouble();
        r.check(prop(r, QStringLiteral("photoPlaceAngle"), "straight").toBool() && std::abs(angle) < 1e-9,
                say("distance: 1.2 mm off over 100 mm (0.7 degrees) snaps straight while placed"), n(angle));
        r.screenshot(QStringLiteral("measurephotoviews_snap_desktop"));
    });
    steps.push_back([&r, st] { r.touchRelease(onStraight(r, st, {140, 114.2}).value_or(QPointF())); });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantList pts = dim(r, 0).value(QStringLiteral("points")).toList();
        const double d = pts.size() == 2 ? measureOf(r).distanceMm(pts[0].toPointF(), pts[1].toPointF()) : 0;
        r.check(dims(r).size() == 1 && pts.size() == 2 && std::abs(pts[0].toPointF().y() - pts[1].toPointF().y()) < 1e-9,
                say("distance: placed straight (both ends at one height on the straightened photo)"));
        r.check(std::abs(d - 100) < 1, say("distance: the bar, 100 mm within 1 %"), n(d));
        r.check(prop(r, QStringLiteral("photoLabel_0"), "straight").toBool() && shown(r, QStringLiteral("photoLabel_0")),
                say("distance: its label says straight"));
        r.check(text(r, QStringLiteral("measureReadoutValue")) == measureOf(r).formatNumber(QStringLiteral("length"), d), say("distance: read out"));
    });
    // A slanted one keeps its angle; with Snap off a nearly straight one too.
    steps.push_back([&r, st] { r.check(tapTable(r, st, {40, 135}), say("slanted: tap one end")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, {140, 135 + 100 * std::tan(10 * kPi / 180)}), say("slanted: tap the other, 10 degrees up")); });
    wait(steps, 1);
    steps.push_back([&r] {
        const double angle = prop(r, QStringLiteral("photoLabel_1"), "angle").toDouble();
        r.check(dims(r).size() == 2 && std::abs(angle - 10) < 0.2 && !prop(r, QStringLiteral("photoLabel_1"), "straight").toBool(),
                say("slanted: a second dimension, 10 degrees, not snapped"), n(angle));
        r.check(shown(r, QStringLiteral("photoLabel_0")) && shown(r, QStringLiteral("photoLabel_1")), say("both stay drawn on the photo"));
        r.check(r.tapItem(QStringLiteral("photoSnap")), say("tap Snap (off)"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        r.check(!photoProp(r, "snap").toBool(), say("Snap is off"));
        r.check(tapTable(r, st, {40, 170}), say("no snap: tap one end"));
    });
    steps.push_back([&r, st] { r.check(tapTable(r, st, {140, 171}), say("no snap: tap the other, 1 mm higher")); });
    wait(steps, 1);
    steps.push_back([&r] {
        const double angle = prop(r, QStringLiteral("photoLabel_2"), "angle").toDouble();
        r.check(dims(r).size() == 3 && std::abs(angle - std::atan(0.01) * 180 / kPi) < 0.1 && !prop(r, QStringLiteral("photoLabel_2"), "straight").toBool(),
                say("no snap: 0.6 degrees stays 0.6 degrees"), n(angle));
        r.check(r.tapItem(QStringLiteral("photoSnap")), say("tap Snap (on again)"));
    });
    wait(steps, 1);

    // ---- several dimensions: listed, selected, renamed, deleted, Keep all
    steps.push_back([&r] {
        r.check(photoProp(r, "snap").toBool(), say("Snap is on again"));
        r.check(shown(r, QStringLiteral("photoDim_0")) && shown(r, QStringLiteral("photoDim_1")) && shown(r, QStringLiteral("photoDim_2"))
                    && text(r, QStringLiteral("photoKeepAll")) == QStringLiteral("Keep all"),
                say("the three are listed under the photo, with Keep all"));
        r.check(dim(r, 0).value(QStringLiteral("letter")).toString() == QStringLiteral("A") && dim(r, 2).value(QStringLiteral("letter")).toString() == QStringLiteral("C"),
                say("lettered A, B, C"));
        r.check(text(r, QStringLiteral("measureReadoutCaption")).contains(QStringLiteral("C · ")), say("the readout shows the last one"),
                text(r, QStringLiteral("measureReadoutCaption")));
        r.screenshot(QStringLiteral("measurephotoviews_dims_desktop"));
        r.check(r.tapItem(QStringLiteral("photoDim_0")), say("tap A in the list"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(photoProp(r, "active").toInt() == 0 && text(r, QStringLiteral("measureReadoutCaption")).contains(QStringLiteral("A · ")),
                say("A is selected and read out"), text(r, QStringLiteral("measureReadoutCaption")));
        r.check(r.tapItem(QStringLiteral("photoDim_0")), say("tap A again"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoDimSheet")) && text(r, QStringLiteral("photoDimName")) == QStringLiteral("A"), say("A opens: its name"));
        r.screenshot(QStringLiteral("measurephotoviews_dim_sheet_desktop"));
        QQuickItem* field = item(r, QStringLiteral("photoDimName"));
        if (field) {
            field->forceActiveFocus();
            QMetaObject::invokeMethod(field, "selectAll");
        }
        r.type(QStringLiteral("Width"));
    });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoDimSave")), say("rename: Save")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("photoDimSheet")) && dim(r, 0).value(QStringLiteral("name")).toString() == QStringLiteral("Width"),
                say("A is named Width"), dim(r, 0).value(QStringLiteral("name")).toString());
        r.check(r.tapItem(QStringLiteral("photoDim_2")), say("tap C"));
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoDim_2")), say("tap C again")); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoDimKeep")), say("C: Keep")); });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantMap kept = measureOf(r).measurements().value(0).toMap();
        r.check(measureOf(r).measurements().size() == 1 && std::abs(kept.value(QStringLiteral("value")).toDouble() - 100) < 1.5,
                say("C: kept on its own (about 100 mm)"), kept.value(QStringLiteral("name")).toString());
        r.check(r.tapItem(QStringLiteral("photoDimDelete")), say("C: Delete"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(dims(r).size() == 2 && photoProp(r, "active").toInt() == -1, say("C is gone, A and B stay"));
        r.check(r.tapItem(QStringLiteral("photoKeepAll")), say("tap Keep all"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        QStringList names;
        bool values = true;
        for (const QVariant& v : measureOf(r).measurements()) {
            const QVariantMap e = v.toMap();
            names << e.value(QStringLiteral("name")).toString();
            const double x = e.value(QStringLiteral("value")).toDouble();
            if (e.value(QStringLiteral("name")).toString() == QStringLiteral("Top Width"))
                values = values && std::abs(x - 100) < 1;
            if (e.value(QStringLiteral("name")).toString() == QStringLiteral("Top B"))
                values = values && std::abs(x - 100 / std::cos(10 * kPi / 180)) < 1;
        }
        r.check(names.contains(QStringLiteral("Top Width")) && names.contains(QStringLiteral("Top B")) && names.size() == 3 && values,
                say("Keep all: kept as Top Width (100 mm) and Top B (101.5 mm)"), names.join(QStringLiteral(", ")));
        r.check(r.tapItem(QStringLiteral("photoMode_outline")), say("Outline"));
    });
    for (int i = 0; i < 4; ++i)
        steps.push_back([&r, st, i] { r.check(tapTable(r, st, kPlate[std::size_t(i)]), say("outline: tap a corner of the plate")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kPlate[0]), say("outline: the first corner again")); });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantMap d = dim(r, 2);
        r.check(dims(r).size() == 3 && d.value(QStringLiteral("kind")).toString() == QStringLiteral("outline") && d.value(QStringLiteral("closed")).toBool()
                    && d.value(QStringLiteral("letter")).toString() == QStringLiteral("D"),
                say("outline: closed, a third dimension, D"));
        r.check(r.tapItem(QStringLiteral("photoOriginButton")), say("tap Origin"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        r.check(photoProp(r, "settingOrigin").toBool() && text(r, QStringLiteral("measureReadoutCaption")).contains(QStringLiteral("origin")),
                say("origin: the next tap sets it"), text(r, QStringLiteral("measureReadoutCaption")));
        r.check(tapTable(r, st, kPlate[3]), say("origin: tap the plate's bottom-left corner"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        const QPointF origin = photoProp(r, "origin").toPointF();
        const auto expected = straightPx(r, st, kPlate[3]);
        r.check(expected && closeTo(origin, *expected, 1.0) && dims(r).size() == 3 && shown(r, QStringLiteral("photoOrigin")),
                say("origin: at the corner (no point added)"), expected ? n(QLineF(origin, *expected).length()) : QString());
        checkRulers(r, st, QStringLiteral("origin"), kPlate[3]);
        auto& m = measureOf(r);
        st->views[0] = {m.straightSize(), m.straightMmPerPixel(), straightPx(r, st, kPlate[2]).value_or(QPointF()),
                        straightPx(r, st, kPlate[0]).value_or(QPointF())};
        r.screenshot(QStringLiteral("measurephotoviews_top_desktop"));
        r.check(r.tapItem(QStringLiteral("photoViewChip_0")), say("tap the view's chip (Top)"));
    });
    wait(steps, 1);

    // ---- views: + View, the Front, switching, Place views
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoViewSheet")) && photoProp(r, "viewName").toString() == QStringLiteral("Top")
                    && photoProp(r, "viewPlane").toString() == QStringLiteral("XY"),
                say("the view: Top, on the ground"));
        r.screenshot(QStringLiteral("measurephotoviews_view_sheet_desktop"));
        r.check(r.tapItem(QStringLiteral("photoViewDone")), say("view: Done"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("photoViewSheet")), say("view: closed"));
        r.check(text(r, QStringLiteral("measureAction_placeImage")) == QStringLiteral("Place in design"), say("one photo: Place in design"));
        r.check(r.tapItem(QStringLiteral("photoAddView")), say("tap + View"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(m.photoViews().size() == 1 && m.photoViewIndex() == -1 && photoProp(r, "stage").toString() == QStringLiteral("start"),
                say("+ View: Top kept in the set, a new photo next"));
        r.check(photoProp(r, "viewName").toString() == QStringLiteral("Front") && photoProp(r, "viewPlane").toString() == QStringLiteral("XZ"),
                say("+ View: the next is the Front, on the Front plane"));
        r.check(shown(r, QStringLiteral("photoStartViews")) && prop(r, QStringLiteral("photoStart"), "title").toString() == QStringLiteral("The Front view"),
                say("the views strip over the next photo's start, which asks for the Front"), prop(r, QStringLiteral("photoStart"), "title").toString());
        r.screenshot(QStringLiteral("measurephotoviews_next_desktop"));
        usePhoto(st, 1);
        r.check(r.tapItem(QStringLiteral("photoChoose")), say("front: Choose photo"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(15000) && measureOf(r).cornersDetected(), say("front: read, the card found")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoStraighten")), say("front: Straighten")); });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(20000), say("front: straightened")); });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(dims(r).isEmpty() && photoProp(r, "origin").isNull(), say("front: a clean photo"));
        r.check(photoProp(r, "mode").toString() == QStringLiteral("outline"), say("front: still Outline"));
    });
    for (int i = 0; i < 4; ++i)
        steps.push_back([&r, st, i] { r.check(tapTable(r, st, kSide[std::size_t(i)]), say("front: tap a corner of the part")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kSide[0]), say("front: the first corner again")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(dim(r, 0).value(QStringLiteral("closed")).toBool(), say("front: closed"));
        r.check(r.tapItem(QStringLiteral("photoUndo")), say("front: Undo"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!dim(r, 0).value(QStringLiteral("closed")).toBool() && dim(r, 0).value(QStringLiteral("points")).toList().size() == 4,
                say("front: Undo opens the outline again"));
        r.check(r.tapItem(QStringLiteral("photoClose")), say("front: Close"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantMap d = dim(r, 0);
        const QVariantMap report = measureOf(r).outlineReport(d.value(QStringLiteral("points")).toList(), true);
        const double area = report.value(QStringLiteral("area")).toDouble();
        r.check(d.value(QStringLiteral("closed")).toBool() && std::abs(area - 3200) < 64, say("front: the part's outline, 80 x 40 mm (3200 mm² within 2 %)"), n(area));
        r.check(r.tapItem(QStringLiteral("photoOriginButton")), say("front: tap Origin"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] { r.check(tapTable(r, st, kSide[3]), say("front: the part's bottom-left corner as the origin")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!photoProp(r, "origin").isNull(), say("front: an origin"));
        r.check(r.tapItem(QStringLiteral("photoViewChip_1")), say("front: tap its chip"));
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoViewPreset_Side")), say("front's sheet: Side")); });
    steps.push_back([&r] {
        r.check(photoProp(r, "viewName").toString() == QStringLiteral("Side") && photoProp(r, "viewPlane").toString() == QStringLiteral("YZ"),
                say("Side: named Side, on the Right plane"));
        r.check(r.tapItem(QStringLiteral("photoViewPlane_XZ")), say("the Front plane"));
    });
    steps.push_back([&r] {
        r.check(photoProp(r, "viewPlane").toString() == QStringLiteral("XZ"), say("on the Front plane"));
        QQuickItem* field = item(r, QStringLiteral("photoViewName"));
        if (field) {
            field->forceActiveFocus();
            QMetaObject::invokeMethod(field, "selectAll");
        }
        r.type(QStringLiteral("Front"));
    });
    steps.push_back([&r] {
        r.check(photoProp(r, "viewName").toString() == QStringLiteral("Front"), say("a typed name: Front"), photoProp(r, "viewName").toString());
        r.check(r.tapItem(QStringLiteral("photoViewClearOrigin")), say("Clear origin"));
    });
    steps.push_back([&r] {
        r.check(photoProp(r, "origin").isNull() && !shown(r, QStringLiteral("photoOrigin")), say("the origin is cleared"));
        r.check(r.tapItem(QStringLiteral("photoViewSheetClose")), say("close the view's sheet"));
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(!shown(r, QStringLiteral("photoViewSheet")) && r.tapItem(QStringLiteral("photoOriginButton")), say("front: Origin again")); });
    wait(steps, 1);
    steps.push_back([&r, st] { r.check(tapTable(r, st, kSide[3]), say("front: the corner again")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        st->views[1] = {m.straightSize(), m.straightMmPerPixel(), straightPx(r, st, kSide[2]).value_or(QPointF()),
                        straightPx(r, st, kSide[0]).value_or(QPointF())};
        r.check(text(r, QStringLiteral("measureAction_placeViews")) == QStringLiteral("Place 2 views"), say("two views: Place 2 views"),
                text(r, QStringLiteral("measureAction_placeViews")));
        r.check(shown(r, QStringLiteral("photoViewChip_0")) && shown(r, QStringLiteral("photoViewChip_1"))
                    && !prop(r, QStringLiteral("photoViewChip_0"), "modelData").toMap().value(QStringLiteral("current")).toBool(),
                say("the strip: Top, and the Front being measured"));
        r.screenshot(QStringLiteral("measurephotoviews_front_desktop"));
        usePhoto(st, 0);
        r.check(r.tapItem(QStringLiteral("photoViewChip_0")), say("tap Top in the strip"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(m.photoViewIndex() == 0 && photoProp(r, "viewName").toString() == QStringLiteral("Top") && photoProp(r, "stage").toString() == QStringLiteral("measure"),
                say("switch: the Top view again"));
        r.check(dims(r).size() == 3 && dim(r, 0).value(QStringLiteral("name")).toString() == QStringLiteral("Width"), say("switch: its three dimensions come back"));
        const auto expected = straightPx(r, st, kPlate[3]);
        r.check(expected && closeTo(photoProp(r, "origin").toPointF(), *expected, 1.0), say("switch: its origin too"));
        r.check(m.photoViews().size() == 2 && m.photoViews()[1].toMap().value(QStringLiteral("state")).toMap().value(QStringLiteral("dims")).toList().size() == 1,
                say("switch: the Front was kept with its outline"));
        usePhoto(st, 1);
        r.check(r.tapItem(QStringLiteral("photoViewChip_1")), say("tap Front in the strip"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(measureOf(r).photoViewIndex() == 1 && dims(r).size() == 1 && photoProp(r, "viewName").toString() == QStringLiteral("Front"),
                say("switch: the Front again, its outline"));
        r.check(r.app().document().references().empty() && r.app().document().sketches().empty(), say("place: nothing in the design yet"));
        r.check(r.tapItem(QStringLiteral("measureAction_placeViews")), say("tap Place 2 views"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoPlaceSheet")) && shown(r, QStringLiteral("photoPlaceRow_0")) && shown(r, QStringLiteral("photoPlaceRow_1")),
                say("place: the two views listed"));
        r.check(text(r, QStringLiteral("photoPlaceAll")).startsWith(QStringLiteral("Photos and 2 outlines")), say("place: photos and the two outlines offered"),
                text(r, QStringLiteral("photoPlaceAll")));
        r.screenshot(QStringLiteral("measurephotoviews_place_sheet_desktop"));
        r.check(r.tapItem(QStringLiteral("photoPlaceAll")), say("tap Photos and 2 outlines as sketches"));
    });
    wait(steps, 4);
    steps.push_back([&r, st] {
        r.check(!shown(r, QStringLiteral("measureScreen")), say("place: the workspace closes into the design"));
        const auto& refs = r.app().document().references();
        r.check(refs.size() == 2, say("place: two reference photos"), QString::number(refs.size()));
        if (refs.size() == 2) {
            const doc::ReferenceImage& top = *refs[0];
            const doc::ReferenceImage& front = *refs[1];
            r.check(top.plane == doc::ReferencePlane::XY && front.plane == doc::ReferencePlane::XZ, say("place: the Top on the ground, the Front on the Front plane"));
            r.check(std::abs(top.width - st->views[0].size.width() * st->views[0].mmPerPixel) < 0.1
                        && std::abs(front.width - st->views[1].size.width() * st->views[1].mmPerPixel) < 0.1,
                    say("place: each at its true size"));
            const QPointF a = onPlane(top, st->views[0], st->views[0].a), b = onPlane(top, st->views[0], st->views[0].b);
            r.check(closeTo(a, {60, 0}, 0.6) && closeTo(b, {0, 50}, 0.6), say("place: the Top's origin corner at the origin (the plate's corners at 60,0 and 0,50 mm)"),
                    QStringLiteral("%1,%2 and %3,%4").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y()));
            const QPointF c = onPlane(front, st->views[1], st->views[1].a), d = onPlane(front, st->views[1], st->views[1].b);
            r.check(closeTo(c, {80, 0}, 0.8) && closeTo(d, {0, 40}, 0.8), say("place: the Front's origin corner at the origin (the part's corners at 80,0 and 0,40 mm)"),
                    QStringLiteral("%1,%2 and %3,%4").arg(c.x()).arg(c.y()).arg(d.x()).arg(d.y()));
        }
        const auto& sketches = r.app().document().sketches();
        r.check(sketches.size() == 2, say("place: the two outlines as sketches"), QString::number(sketches.size()));
        if (sketches.size() == 2) {
            const QRectF topBox = pointsBox(*sketches[0]), frontBox = pointsBox(*sketches[1]);
            const auto& plane = sketches[1]->plane();
            r.check(sketches[0]->plane().normal().z > 0.99 && closeTo(topBox.topLeft(), {0, 0}, 0.8) && closeTo(topBox.bottomRight(), {60, 50}, 0.8),
                    say("place: the plate's outline on the ground, 0..60 x 0..50 mm"),
                    QStringLiteral("%1,%2 to %3,%4").arg(topBox.left()).arg(topBox.top()).arg(topBox.right()).arg(topBox.bottom()));
            r.check(std::abs(plane.yAxis.z - 1) < 1e-9 && std::abs(plane.xAxis.x - 1) < 1e-9 && closeTo(frontBox.topLeft(), {0, 0}, 0.8)
                        && closeTo(frontBox.bottomRight(), {80, 40}, 0.8),
                    say("place: the part's side on the Front plane, 0..80 x 0..40 mm (up is up)"),
                    QStringLiteral("%1,%2 to %3,%4").arg(frontBox.left()).arg(frontBox.top()).arg(frontBox.right()).arg(frontBox.bottom()));
        }
        r.check(!r.app().sketchMode(), say("place: no sketch left open"));
        r.screenshot(QStringLiteral("measurephotoviews_placed_desktop"));
        r.app().setView(QStringLiteral("front"));
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.screenshot(QStringLiteral("measurephotoviews_placed_front_desktop"));
        for (int i = 0; i < 4; ++i)
            r.app().undo();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().document().references().empty() && r.app().document().sketches().empty(), say("place: four undos take it all back"));
        r.app().newDocument();
    });
    wait(steps, 2);
    // Back to the Top view for the pictures at every size (a dimension selected).
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("back to Measure")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        usePhoto(st, 0);
        r.check(r.tapItem(QStringLiteral("photoViewChip_0")), say("the Top view"));
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoDim_1")), say("select B")); });
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureClose")), say("close")); });
    wait(steps, 1);

    // ---- every size
    for (const Size* s : {&kPhone, &kDuoOuter, &kDuoInner, &kDuoTurned, &kIpad, &kDesktop}) {
        steps.push_back([&r, s] { applySize(r, *s); });
        wait(steps, 3);
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("measureButton")), say(s->name + QStringLiteral(": open"))); });
        wait(steps, 2);
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("measureTab_photo")), say(s->name + QStringLiteral(": Photo"))); });
        wait(steps, 2);
        steps.push_back([&r, s] {
            r.check(photoProp(r, "stage").toString() == QStringLiteral("measure") && dims(r).size() == 3, say(s->name + QStringLiteral(": the Top view measured")));
            checkPhotoLayout(r, *s, s->name);
            r.screenshot(QStringLiteral("measurephotoviews_%1").arg(s->name));
        });
        if (s == &kPhone || s == &kDuoOuter) {
            steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("photoViewChip_0")), say(s->name + QStringLiteral(": the view's sheet"))); });
            wait(steps, 1);
            steps.push_back([&r, s] {
                const QRectF panel = sceneRect(item(r, QStringLiteral("photoViewSheetPanel")));
                const QRectF safe(s->safe[3], s->safe[0], s->width - s->safe[1] - s->safe[3], s->height - s->safe[0] - s->safe[2]);
                r.check(shown(r, QStringLiteral("photoViewSheet")) && safe.adjusted(-0.5, -0.5, 0.5, 0.5).contains(panel) && shown(r, QStringLiteral("photoViewCorners")),
                        say(s->name + QStringLiteral(": the view's sheet fits, Back to corners in it")));
                r.screenshot(QStringLiteral("measurephotoviews_view_sheet_%1").arg(s->name));
                r.check(press(r, *s, QStringLiteral("photoViewDone")), say(s->name + QStringLiteral(": Done")));
            });
            wait(steps, 1);
            steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("measureAction_placeViews")), say(s->name + QStringLiteral(": Place 2 views"))); });
            wait(steps, 1);
            steps.push_back([&r, s] {
                const QRectF panel = sceneRect(item(r, QStringLiteral("photoPlaceSheetPanel")));
                const QRectF safe(s->safe[3], s->safe[0], s->width - s->safe[1] - s->safe[3], s->height - s->safe[0] - s->safe[2]);
                r.check(shown(r, QStringLiteral("photoPlaceSheet")) && safe.adjusted(-0.5, -0.5, 0.5, 0.5).contains(panel),
                        say(s->name + QStringLiteral(": the place sheet fits")));
                r.screenshot(QStringLiteral("measurephotoviews_place_sheet_%1").arg(s->name));
                r.check(press(r, *s, QStringLiteral("photoPlaceSheetClose")), say(s->name + QStringLiteral(": close the place sheet")));
            });
            wait(steps, 1);
            steps.push_back([&r, s] { r.check(!shown(r, QStringLiteral("photoPlaceSheet")), say(s->name + QStringLiteral(": closed"))); });
        }
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("measureClose")), say(s->name + QStringLiteral(": close"))); });
        wait(steps, 1);
    }

    // ---- Photos only; Back to corners and Remove view from the view's sheet; Clear
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureButton")), say("desktop: open")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureAction_placeViews")), say("desktop: Place 2 views")); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("photoPlacePhotos")), say("desktop: Photos only")); });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.app().document().references().size() == 2 && r.app().document().sketches().empty(), say("photos only: two photos, no sketch"));
        r.app().undo();
        r.app().undo();
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(r.app().document().references().empty(), say("photos only: undone"));
        r.app().newDocument();
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureButton")), say("desktop: open again")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("photoViewChip_0")), say("desktop: the Top view's sheet")); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("photoViewCorners")), say("desktop: Back to corners")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(photoProp(r, "stage").toString() == QStringLiteral("corners") && measureOf(r).photoViewIndex() == 0, say("back to the Top's corners"));
        r.check(r.clickItem(QStringLiteral("photoStraighten")), say("desktop: Straighten again"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForPhoto(20000), say("straightened again")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("photoViewChip_0")), say("desktop: the Top view's sheet again")); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("photoViewRemove")), say("desktop: Remove view")); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.photoViews().size() == 1 && m.photoViewIndex() == -1 && m.photoViews()[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Front"),
                say("Remove view: the Front is left in the set, the photo still measured"));
        r.app().setTouchMode(true);
    });
    wait(steps, 1);
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarA), say("a distance: one end")); });
    steps.push_back([&r, st] { r.check(tapTable(r, st, kBarB), say("a distance: the other")); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!dims(r).isEmpty(), say("a dimension to clear"));
        r.check(r.tapItem(QStringLiteral("photoClear")), say("Clear"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(dims(r).isEmpty(), say("Clear takes every dimension"));
        r.check(r.tapItem(QStringLiteral("measureClose")), say("close"));
    });
    wait(steps, 1);

    // ---- clean up for the next scenarios
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        m.clearPhoto();
        m.clearPhotoViews();
        for (const QVariant& e : m.measurements())
            m.removeMeasurement(e.toMap().value(QStringLiteral("id")).toString());
        m.setDevices(ui::platformMeasureDevices());
        if (QQuickItem* screen = item(r, QStringLiteral("measureScreen")))
            QMetaObject::invokeMethod(screen, "resetForTest");
        r.app().newDocument();
        st->dir.reset();
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measurephotoviews"), 113, steps});

} // namespace
} // namespace os::app
