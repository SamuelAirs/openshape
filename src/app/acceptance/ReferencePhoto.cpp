// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference photos (doc::ReferenceImage): a generated picture with a known
// 100 mm square added at a known scale (addReferenceImage, the Measure
// workspace's call) and through File -> Add Reference Photo...; read back
// from the rendered frame where the square projects in Top view, hidden
// behind a box standing on it; its Model panel values changed by click and by
// tap (desktop, phone 402x874, iPad 1180x820); Set scale by two points (mouse,
// and a finger with the loupe); hide, show, delete, undo and redo; saved and
// reopened; a sketch drawn over it.

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QFile>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

// The picture: 800 x 600 px at 0.25 mm per pixel (200 x 150 mm), white with
// a grey 10 mm grid, a red 100 mm square (px 100..500 x 100..500) and a blue
// patch (px 560..760 x 100..300). Centred on the origin of the ground (its
// top row towards +Y): the square spans x -75..25, y -50..50 mm, the patch
// x 40..90, y 0..50.
constexpr double kMmPerPixel = 0.25;

QString writePicture(const QString& path, bool small = false)
{
    QImage image(small ? 400 : 800, small ? 300 : 600, QImage::Format_RGB32);
    image.fill(QColor(250, 250, 250));
    QPainter p(&image);
    p.setPen(QPen(QColor(200, 200, 200), 1));
    for (int x = 0; x < image.width(); x += 40)
        p.drawLine(x, 0, x, image.height());
    for (int y = 0; y < image.height(); y += 40)
        p.drawLine(0, y, image.width(), y);
    const int s = small ? 2 : 1;
    p.fillRect(QRect(100 / s, 100 / s, 400 / s, 400 / s), QColor(225, 30, 30));
    p.fillRect(QRect(560 / s, 100 / s, 200 / s, 200 / s), QColor(30, 60, 225));
    p.end();
    image.save(path);
    return path;
}

struct State {
    QString picture, small, project, unreadable;
    Uuid photo;
    double width = 0;
    QStringList messages;
    QMetaObject::Connection listening;
};

QString idText(const Uuid& id)
{
    return QString::fromStdString(id.toString());
}

// The average colour around a point of the frame (5 x 5 device pixels).
QColor colourAt(const QImage& image, QPointF logical)
{
    const double dpr = image.devicePixelRatio();
    const int cx = int(std::lround(logical.x() * dpr)), cy = int(std::lround(logical.y() * dpr));
    constexpr int r = 2;
    if (cx - r < 0 || cy - r < 0 || cx + r >= image.width() || cy + r >= image.height())
        return {};
    double red = 0, green = 0, blue = 0;
    for (int y = cy - r; y <= cy + r; ++y)
        for (int x = cx - r; x <= cx + r; ++x) {
            const QColor c = image.pixelColor(x, y);
            red += c.red();
            green += c.green();
            blue += c.blue();
        }
    const double n = (2 * r + 1) * (2 * r + 1);
    return QColor(int(red / n), int(green / n), int(blue / n));
}

QString colourText(const QColor& c)
{
    return QStringLiteral("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

bool reddish(const QColor& c)
{
    return c.isValid() && c.red() - c.green() > 50 && c.red() - c.blue() > 50;
}
bool bluish(const QColor& c)
{
    return c.isValid() && c.blue() - c.red() > 40;
}
bool neutral(const QColor& c)
{
    return c.isValid() && std::abs(c.red() - c.green()) < 25 && std::abs(c.red() - c.blue()) < 25;
}

const doc::ReferenceImage* photoOf(AcceptanceRunner& r, const State& s)
{
    return r.app().document().reference(s.photo);
}

bool tap(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item || !item->isVisible())
        return false;
    r.touchTap({item->mapToScene(QPointF(item->width() / 2, item->height() / 2))});
    return true;
}

bool tapKeys(AcceptanceRunner& r, const QString& keys)
{
    bool all = true;
    for (const QChar c : keys)
        all = tap(r, QStringLiteral("keypadKey_") + c) && all;
    return all;
}

void wait(Steps& steps, int n)
{
    for (int i = 0; i < n; ++i)
        steps.push_back([] {});
}

// ---- Desktop: add, see, edit, Set scale, hide, delete, save, sketch --------------------------

void desktop(Steps& steps, AcceptanceRunner& r, const std::shared_ptr<State>& s)
{
    steps.push_back([&r, s] {
        const QString dir = r.outputDir();
        s->picture = writePicture(dir + QStringLiteral("/referencephoto_grid.png"));
        s->small = writePicture(dir + QStringLiteral("/referencephoto_small.png"), true);
        s->project = dir + QStringLiteral("/referencephoto.openshape");
        s->unreadable = dir + QStringLiteral("/referencephoto_not_a_picture.png");
        QFile bad(s->unreadable);
        if (bad.open(QIODevice::WriteOnly))
            bad.write("this is not a picture");
        s->listening = QObject::connect(&r.app(), &ui::AppController::message, &r.app(), [s](const QString& t) { s->messages << t; });
        r.app().setPerspective(false);
        r.check(!r.app().addReferenceImage(s->unreadable, 0.25), "referencephoto: an unreadable picture is refused");
        r.check(!s->messages.isEmpty() && s->messages.last().contains(QStringLiteral("could not be read")),
                "referencephoto: ...with a message", s->messages.join(QStringLiteral(" | ")));
        r.check(r.app().document().references().empty(), "referencephoto: nothing added");
        r.check(r.app().addReferenceImage(s->picture, kMmPerPixel, QStringLiteral("XY")), "referencephoto: addReferenceImage");
        r.check(r.app().document().references().size() == 1, "referencephoto: one photo");
        if (r.app().document().references().empty())
            return;
        const doc::ReferenceImage& photo = *r.app().document().references().front();
        s->photo = photo.id();
        r.check(photo.name == "Photo 1" && std::abs(photo.width - 200) < 1e-9 && std::abs(photo.height() - 150) < 1e-9
                    && photo.plane == doc::ReferencePlane::XY && !photo.unscaled && std::abs(photo.opacity - 0.6) < 1e-9,
                "referencephoto: 200 x 150 mm on the ground, 60 %",
                AcceptanceRunner::num(photo.width) + QStringLiteral(" x ") + AcceptanceRunner::num(photo.height()));
        r.check(photo.image && photo.image->format == "png" && photo.image->pixelWidth == 800, "referencephoto: the PNG kept as it is");
        r.check(r.app().undoText().contains(QStringLiteral("photo"), Qt::CaseInsensitive), "referencephoto: one undo step",
                r.app().undoText());
    });
    wait(steps, 3);
    // In Top view (the add faced it): the square's colours where it projects.
    steps.push_back([&r] {
        const Camera& camera = r.app().interaction().camera();
        r.check(camera.backward().z > 0.999, "referencephoto: the view looks down on the photo");
        const QImage frame = r.window()->grabWindow();
        const QColor square = colourAt(frame, r.screenPoint(-25, 20, 0));
        const QColor corner = colourAt(frame, r.screenPoint(-70, -45, 0));
        const QColor patch = colourAt(frame, r.screenPoint(65, 25, 0));
        const QColor paper = colourAt(frame, r.screenPoint(65, -40, 0));
        const QColor outside = colourAt(frame, r.screenPoint(120, -20, 0));
        r.check(reddish(square) && reddish(corner), "referencephoto: the red square where it projects",
                colourText(square) + QStringLiteral(", ") + colourText(corner));
        r.check(bluish(patch), "referencephoto: the blue patch where it projects", colourText(patch));
        r.check(neutral(paper) && neutral(outside), "referencephoto: paper and background are neutral",
                colourText(paper) + QStringLiteral(", ") + colourText(outside));
        // Semi-transparent: lighter than the picture's own red.
        r.check(square.green() > 60, "referencephoto: 60 % lets the background through", colourText(square));
        r.screenshot(QStringLiteral("referencephoto_01_top"));
    });
    // A box standing on part of the square hides it there; a click on the
    // photo selects nothing (it is not geometry), on the box its face.
    steps.push_back([&r, s] {
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = {-75, -50, 0};
        box->size = {30, 30, 10};
        auto& stack = r.app().interaction().undoStack();
        r.check(stack.push(std::make_unique<cmd::CreateBodyCommand>("Block", std::move(box)), r.app().document()).ok(),
                "referencephoto: a box over the square's corner");
        r.app().interaction().documentChanged();
        r.key(Qt::Key_Escape);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        const QImage frame = r.window()->grabWindow();
        const QColor onBox = colourAt(frame, r.screenPoint(-60, -35, 10));
        const QColor besideBox = colourAt(frame, r.screenPoint(-25, 20, 0));
        r.check(neutral(onBox), "referencephoto: the box in front hides the photo", colourText(onBox));
        r.check(reddish(besideBox), "referencephoto: beside the box it shows", colourText(besideBox));
        r.click(r.screenPoint(-10, 20, 0));
        r.check(r.app().interaction().selection().empty(), "referencephoto: a click on the photo selects nothing");
        r.click(r.screenPoint(-60, -35, 10));
        const auto& sel = r.app().interaction().selection();
        r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Face, "referencephoto: a click on the box picks its face");
        r.screenshot(QStringLiteral("referencephoto_02_box_hides"));
        r.key(Qt::Key_Escape);
        r.app().setView(QStringLiteral("iso"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.screenshot(QStringLiteral("referencephoto_03_iso"));
        r.app().setView(QStringLiteral("bottom"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        const QImage frame = r.window()->grabWindow();
        const QColor from = colourAt(frame, r.screenPoint(-25, 20, 0));
        r.check(from.isValid() && from.red() - from.green() > 15, "referencephoto: seen from below it shows, dimmer", colourText(from));
        r.screenshot(QStringLiteral("referencephoto_04_below"));
        r.app().setView(QStringLiteral("top"));
    });
    wait(steps, 2);

    // ---- The Model panel: its row, values by click ----
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("historyRow_") + idText(s->photo)), "referencephoto: its Model panel row");
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_opacity")), "referencephoto: Opacity field");
        r.type(QStringLiteral("35"));
        r.key(Qt::Key_Return);
    });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->opacity - 0.35) < 1e-9, "referencephoto: opacity 35 %", p ? AcceptanceRunner::num(p->opacity) : QString());
        r.check(r.clickItem(QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_rotation")), "referencephoto: Rotation field");
        r.type(QStringLiteral("90"));
        r.key(Qt::Key_Return);
    });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->rotation - kPi / 2) < 1e-9, "referencephoto: turned 90 degrees", p ? AcceptanceRunner::num(p->rotation) : QString());
        r.screenshot(QStringLiteral("referencephoto_05_turned"));
        r.check(r.clickItem(QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_width")), "referencephoto: Width field");
        r.type(QStringLiteral("400"));
        r.key(Qt::Key_Return);
    });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->width - 400) < 1e-9 && std::abs(p->height() - 300) < 1e-9, "referencephoto: 400 mm wide, 300 high",
                p ? AcceptanceRunner::num(p->width) : QString());
        r.check(r.clickItem(QStringLiteral("historyChoice_") + idText(s->photo) + QStringLiteral("_plane_Front")), "referencephoto: Plane: Front");
    });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && p->plane == doc::ReferencePlane::XZ, "referencephoto: on the Front plane");
        r.screenshot(QStringLiteral("referencephoto_06_front_plane"));
        // Undo, one step each: the plane, the width, the rotation (the width stays 400 for Set scale).
        r.key(Qt::Key_Z, Qt::ControlModifier);
    });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && p->plane == doc::ReferencePlane::XY && std::abs(p->width - 400) < 1e-9, "referencephoto: undo: back on the ground");
        r.check(r.clickItem(QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_rotation")), "referencephoto: Rotation field again");
        r.type(QStringLiteral("0"));
        r.key(Qt::Key_Return);
    });
    // ---- Set scale by two points (mouse): the square's bottom edge is 200 mm at 400 wide; it is 100.
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->rotation) < 1e-9, "referencephoto: straight again");
        r.check(r.clickItem(QStringLiteral("historySetScale_") + idText(s->photo)), "referencephoto: Set scale");
    });
    wait(steps, 3);
    steps.push_back([&r] {
        const QVariantMap info = r.app().referenceScale();
        r.check(info.value(QStringLiteral("active")).toBool() && info.value(QStringLiteral("step")).toInt() == 0,
                "referencephoto: Set scale waits for the first point");
        QQuickItem* hint = r.findItem(QStringLiteral("referenceScaleHint"));
        r.check(hint && hint->isVisible() && hint->property("text").toString().contains(QStringLiteral("Click")),
                "referencephoto: the card says what to click", hint ? hint->property("text").toString() : QString());
        // At 400 mm wide the square spans x -150..50, y -100..100.
        r.click(r.screenPoint(-150, -100, 0));
    });
    steps.push_back([&r] {
        r.check(r.app().referenceScale().value(QStringLiteral("step")).toInt() == 1, "referencephoto: the first point placed");
        r.mouseMove(r.screenPoint(0, -100, 0));
    });
    steps.push_back([&r] {
        r.screenshot(QStringLiteral("referencephoto_07_scale_rubber_band"));
        r.click(r.screenPoint(50, -100, 0));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const QVariantMap info = r.app().referenceScale();
        r.check(info.value(QStringLiteral("step")).toInt() == 2, "referencephoto: both points placed");
        r.check(info.value(QStringLiteral("measured")).toString().startsWith(QStringLiteral("20")), "referencephoto: about 200 mm measured",
                info.value(QStringLiteral("measured")).toString());
        QQuickItem* field = r.findItem(QStringLiteral("referenceScaleDistance"));
        r.check(field && field->isVisible() && field->hasActiveFocus(), "referencephoto: the distance field takes the keys");
        r.screenshot(QStringLiteral("referencephoto_08_scale_distance"));
        r.type(QStringLiteral("100"));
        r.check(r.clickItem(QStringLiteral("referenceScaleApply")), "referencephoto: Set scale button");
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->width - 200) < 2.0, "referencephoto: Set scale: 200 mm wide again (the square is 100 mm)",
                p ? AcceptanceRunner::num(p->width) : QString());
        r.check(!r.app().referenceScale().value(QStringLiteral("active")).toBool(), "referencephoto: Set scale ends");
        r.check(r.app().undoText().contains(QStringLiteral("scale"), Qt::CaseInsensitive), "referencephoto: one undo step", r.app().undoText());
        s->width = p ? p->width : 0;
        // Exactly as made, for the pixel checks below.
        r.check(r.clickItem(QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_width")), "referencephoto: Width field");
        r.type(QStringLiteral("200"));
        r.key(Qt::Key_Return);
    });
    // Esc ends Set scale without a change.
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("historySetScale_") + idText(s->photo)), "referencephoto: Set scale again"); });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.click(r.screenPoint(-75, -50, 0));
        r.key(Qt::Key_Escape);
        r.check(!r.app().referenceScale().value(QStringLiteral("active")).toBool(), "referencephoto: Esc ends Set scale");
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->width - 200) < 1e-9, "referencephoto: ...changing nothing");
    });
    // ---- Hide, show, delete, undo, redo ----
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("historyVisibility_") + idText(s->photo)), "referencephoto: Hide"); });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && !p->visible, "referencephoto: hidden");
        r.check(r.clickItem(QStringLiteral("historyRow_") + idText(s->photo)), "referencephoto: fold the row (no highlight)");
        r.mouseMove(r.screenPoint(60, -60, 0)); // off the row: hovering it would show the hidden photo
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        const QImage frame = r.window()->grabWindow();
        const QColor square = colourAt(frame, r.screenPoint(-25, 20, 0));
        r.check(neutral(square), "referencephoto: hidden: no red", colourText(square));
        r.check(r.clickItem(QStringLiteral("historyRow_") + idText(s->photo)), "referencephoto: open the row again");
    });
    wait(steps, 1);
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("historyVisibility_") + idText(s->photo)), "referencephoto: Show"); });
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && p->visible, "referencephoto: shown");
        r.check(r.clickItem(QStringLiteral("historyDelete_") + idText(s->photo)), "referencephoto: Delete");
    });
    steps.push_back([&r] {
        r.check(r.app().document().references().empty(), "referencephoto: deleted");
        const QImage frame = r.window()->grabWindow();
        r.check(neutral(colourAt(frame, r.screenPoint(-25, 20, 0))), "referencephoto: deleted: no red in the view");
        r.key(Qt::Key_Z, Qt::ControlModifier);
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(photoOf(r, *s) != nullptr, "referencephoto: undo brings it back");
        const QImage frame = r.window()->grabWindow();
        r.check(reddish(colourAt(frame, r.screenPoint(-25, 20, 0))), "referencephoto: ...and draws it again");
        r.key(Qt::Key_Y, Qt::ControlModifier);
    });
    steps.push_back([&r] {
        r.check(r.app().document().references().empty(), "referencephoto: redo deletes it again");
        r.key(Qt::Key_Z, Qt::ControlModifier);
    });
    // ---- File -> Add Reference Photo... (the dialog's choice prepared) ----
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), "referencephoto: File menu"); });
    steps.push_back([&r, s] {
        r.app().setNextFileChoice(QUrl::fromLocalFile(s->small));
        r.check(r.clickItem(QStringLiteral("addReferencePhotoMenuItem")), "referencephoto: File -> Add Reference Photo...");
    });
    wait(steps, 3);
    steps.push_back([&r, s] {
        const auto& refs = r.app().document().references();
        r.check(refs.size() == 2, "referencephoto: a second photo", QString::number(refs.size()));
        if (refs.size() == 2) {
            const doc::ReferenceImage& second = *refs[1];
            r.check(second.name == "Photo 2" && second.unscaled && std::abs(second.width - 200) < 1e-9,
                    "referencephoto: Photo 2, 200 mm wide, scale not set", QString::fromStdString(second.name));
            r.app().deleteHistoryItem(QStringLiteral("reference"), idText(second.id()));
        }
        r.check(refs.size() == 1, "referencephoto: the second deleted from its row's call");
    });
    // ---- Save and reopen ----
    steps.push_back([&r, s] {
        r.check(r.app().saveProjectAs(QUrl::fromLocalFile(s->project)), "referencephoto: save");
        r.check(r.app().openProject(QUrl::fromLocalFile(s->project)) && r.waitForOpen(), "referencephoto: reopen");
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && p->hasImage() && std::abs(p->width - 200) < 1e-9 && std::abs(p->opacity - 0.35) < 1e-9 && std::abs(p->rotation) < 1e-9
                    && p->plane == doc::ReferencePlane::XY && std::abs(p->center.x) < 1e-9,
                "referencephoto: reopened: same picture and placement");
        r.check(r.app().bodyCount() == 1, "referencephoto: and the box");
        r.app().setView(QStringLiteral("top"));
        r.app().fitAll();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        const QImage frame = r.window()->grabWindow();
        r.check(reddish(colourAt(frame, r.screenPoint(-25, 20, 0))), "referencephoto: reopened: drawn where it was");
        r.screenshot(QStringLiteral("referencephoto_09_reopened"));
    });
    // ---- A sketch on the ground over it ----
    steps.push_back([&r] {
        r.key(Qt::Key_Escape);
        r.app().startSketch(QStringLiteral("top"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().sketchMode(), "referencephoto: a sketch on the ground");
        r.app().setSketchTool(QStringLiteral("line"));
        r.app().interaction().fitAll(false); // the sketch framed the box's top
    });
    wait(steps, 1);
    steps.push_back([&r] { r.click(r.screenPoint(-75, 60, 0)); });
    steps.push_back([&r] { r.click(r.screenPoint(25, 60, 0)); });
    steps.push_back([&r] {
        r.key(Qt::Key_Escape);
        const QImage frame = r.window()->grabWindow();
        r.check(reddish(colourAt(frame, r.screenPoint(-25, 20, 0))), "referencephoto: the photo shows under the sketch");
        r.screenshot(QStringLiteral("referencephoto_10_sketch_over"));
    });
    steps.push_back([&r] {
        r.app().finishSketch();
        const auto& sketches = r.app().document().sketches();
        r.check(!sketches.empty() && !sketches.back()->lines().empty(), "referencephoto: the line traced over the photo is kept");
    });
}

// ---- Touch: phone and iPad (values by tap; Set scale with a finger and the loupe) -----------

struct Config {
    QString name;
    int width, height;
    QVariant safeArea;
    bool phone() const { return width < 600; }
};

void touch(Steps& steps, AcceptanceRunner& r, const Config c, const std::shared_ptr<State>& s)
{
    steps.push_back([&r, c] {
        r.app().newDocument();
        r.app().setTouchMode(true);
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
        r.app().setPerspective(false);
    });
    wait(steps, 3);
    steps.push_back([&r, c, s] {
        r.check(r.app().addReferenceImage(QUrl::fromLocalFile(s->picture).toString(), kMmPerPixel, QStringLiteral("XY")),
                c.name + QStringLiteral(": add the photo (a file URL)"));
        s->photo = r.app().document().references().empty() ? Uuid() : r.app().document().references().front()->id();
        r.app().setTouchMode(true);
    });
    wait(steps, 3);
    steps.push_back([&r, c] {
        const QImage frame = r.window()->grabWindow();
        r.check(reddish(colourAt(frame, r.screenPoint(-25, 20, 0))), c.name + QStringLiteral(": the square in sight"));
        if (c.phone())
            r.check(tap(r, QStringLiteral("modelPanelButton")), c.name + QStringLiteral(": tap Model"));
    });
    wait(steps, 3);
    steps.push_back([&r, c, s] { r.check(tap(r, QStringLiteral("historyRow_") + idText(s->photo)), c.name + QStringLiteral(": tap the photo's row")); });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        r.check(tap(r, QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_opacity")), c.name + QStringLiteral(": tap Opacity"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        QQuickItem* pad = r.findItem(QStringLiteral("numericKeypad"));
        r.check(pad && pad->isVisible(), c.name + QStringLiteral(": the keypad types the opacity"));
        r.screenshot(QStringLiteral("referencephoto_") + c.name + QStringLiteral("_opacity"));
        r.check(tapKeys(r, QStringLiteral("80")) && tap(r, QStringLiteral("keypadKey_done")), c.name + QStringLiteral(": tap 8, 0 and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->opacity - 0.8) < 1e-9, c.name + QStringLiteral(": opacity 80 %"), p ? AcceptanceRunner::num(p->opacity) : QString());
        r.check(tap(r, QStringLiteral("historyParam_") + idText(s->photo) + QStringLiteral("_width")), c.name + QStringLiteral(": tap Width"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(tapKeys(r, QStringLiteral("400")) && tap(r, QStringLiteral("keypadKey_done")), c.name + QStringLiteral(": tap 4, 0, 0 and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->width - 400) < 1e-9, c.name + QStringLiteral(": 400 mm wide"), p ? AcceptanceRunner::num(p->width) : QString());
        QQuickItem* button = r.findItem(QStringLiteral("historySetScale_") + idText(s->photo));
        r.check(button && button->height() >= 30, c.name + QStringLiteral(": Set scale is a touch-size button"),
                button ? QString::number(button->height()) : QString());
        r.check(tap(r, QStringLiteral("historySetScale_") + idText(s->photo)), c.name + QStringLiteral(": tap Set scale"));
    });
    wait(steps, 4);
    // A finger on one end: the loupe shows what is under it; lifted, the point stays.
    steps.push_back([&r, c] {
        r.check(r.app().referenceScale().value(QStringLiteral("active")).toBool(), c.name + QStringLiteral(": Set scale runs"));
        if (c.phone())
            r.check(!r.window()->property("historyOpen").toBool(), c.name + QStringLiteral(": the Model panel steps aside"));
        r.touchPress(r.screenPoint(-150, -95, 0));
    });
    steps.push_back([&r] { r.touchMove(r.screenPoint(-150, -98, 0)); });
    steps.push_back([&r] { r.touchMove(r.screenPoint(-150, -100, 0)); });
    steps.push_back([&r, c] {
        QQuickItem* loupe = r.findItem(QStringLiteral("referenceScaleLoupe"));
        r.check(loupe && loupe->isVisible(), c.name + QStringLiteral(": the loupe shows while the finger places the point"));
        r.screenshot(QStringLiteral("referencephoto_") + c.name + QStringLiteral("_loupe"));
        r.touchRelease(r.screenPoint(-150, -100, 0));
    });
    steps.push_back([&r, c] {
        r.check(r.app().referenceScale().value(QStringLiteral("step")).toInt() == 1, c.name + QStringLiteral(": the first point placed"));
        r.touchTap({r.screenPoint(50, -100, 0)});
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().referenceScale().value(QStringLiteral("step")).toInt() == 2, c.name + QStringLiteral(": the second point placed"));
        QQuickItem* pad = r.findItem(QStringLiteral("numericKeypad"));
        r.check(pad && pad->isVisible(), c.name + QStringLiteral(": the keypad types the distance"));
        r.screenshot(QStringLiteral("referencephoto_") + c.name + QStringLiteral("_distance"));
        r.check(tapKeys(r, QStringLiteral("100")) && tap(r, QStringLiteral("keypadKey_done")), c.name + QStringLiteral(": tap 1, 0, 0 and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        const auto* p = photoOf(r, *s);
        r.check(p && std::abs(p->width - 200) < 3.0, c.name + QStringLiteral(": Set scale by finger: about 200 mm wide again"),
                p ? AcceptanceRunner::num(p->width) : QString());
        r.check(!r.app().referenceScale().value(QStringLiteral("active")).toBool(), c.name + QStringLiteral(": Set scale ends"));
    });
}

Steps referenceSteps(AcceptanceRunner& r)
{
    Steps out;
    auto state = std::make_shared<State>();
    desktop(out, r, state);
    touch(out, r, Config{QStringLiteral("iphone"), 402, 874, QVariantList{62, 0, 34, 0}}, state);
    touch(out, r, Config{QStringLiteral("ipad"), 1180, 820, QVariant()}, state);
    out.push_back([&r, state] {
        QObject::disconnect(state->listening);
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
        r.app().setTouchMode(false);
        r.app().setPerspective(true);
    });
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("referencephoto"), 120, referenceSteps});

} // namespace
} // namespace os::app
