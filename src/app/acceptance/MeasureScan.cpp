// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Measure's Scan (docs/SCAN.md) through the real UI, with synthetic photos
// (measure/ScanSynthetic: a 50 x 40 x 25 mm box standing on A4, twelve
// photos all around from 35 to 55 degrees up, the lens unknown: estimated) written as image files, Vision's rectangle finder and
// subject masks stood in for. The desktop: the photos opened through the
// file dialog (stubbed), one photo's corners reviewed and a handle dragged
// (the magnifier), Build with its progress, the result within the engine's
// tolerance, Add to design (a body of that size), undo; Build cancelled;
// photos from one side only warned about. The phone: Take photos (the
// camera stand-in) and Choose more (the library, several at once), Build.
// Legal paper picked, and twelve photos of the box on a yellow lined legal
// pad on a wooden table found by FoldForm's own paper finder (no Vision)
// and built. Every stage at phone, iPhone Duo outer and inner and desktop
// sizes: pictures measurescan_<stage>_<size>.png.

#include "app/AcceptanceRunner.h"
#include "core/Math.h"
#include "document/Body.h"
#include "geometry/Modeling.h"
#include "measure/Homography.h"
#include "measure/ScanSynthetic.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLineF>
#include <QtCore/QMap>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;
namespace synth = measure::synthetic;

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
const Size kDesktop{QStringLiteral("desktop"), 1400, 900, {0, 0, 0, 0}, false};

// The box on the paper (x along the sheet's long side).
const Vec3 kBox{50, 40, 25};

struct State {
    std::unique_ptr<QTemporaryDir> dir;
    QStringList allRound; // twelve photos all around
    QStringList oneSide;  // six from one side
    QStringList legal;    // twelve on a yellow lined legal pad (no Vision: the paper finder)
    QMap<QString, ui::RectangleCorners> corners; // by file stem ("allround_03")
    QMap<QString, QImage> masks;
    QMap<QString, ui::RectangleCorners> truth; // every photo's corners
    int picked = 0;
    double maxProgress = 0;
    bool sawProgress = false;
    QString lastOutcome;
    QMetaObject::Connection progressWatch, finishedWatch;
    QPointF corner;
    double w = 0, d = 0, h = 0; // the result's size
};
using StatePtr = std::shared_ptr<State>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

QString say(const QString& text) { return QStringLiteral("measurescan: ") + text; }

ui::MeasureController& measureOf(AcceptanceRunner& r) { return *r.app().measure(); }

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = r.findItem(name);
    return i && i->isVisible();
}

QString text(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = r.findItem(name);
    return i ? i->property("text").toString() : QString();
}

QPointF center(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = r.findItem(name);
    return i ? i->mapRectToScene(QRectF(0, 0, i->width(), i->height())).center() : QPointF();
}

QString current(AcceptanceRunner& r)
{
    QQuickItem* screen = r.findItem(QStringLiteral("measureScreen"));
    return screen ? screen->property("current").toString() : QString();
}

bool press(AcceptanceRunner& r, const Size& s, const QString& name) { return s.touch ? r.tapItem(name) : r.clickItem(name); }

void applySize(AcceptanceRunner& r, const Size& s)
{
    r.app().setTouchMode(s.touch);
    r.resizeWindow(s.width, s.height);
    const bool none = s.safe[0] == 0 && s.safe[1] == 0 && s.safe[2] == 0 && s.safe[3] == 0;
    r.window()->setProperty("simulatedSafeArea", none ? QVariant() : QVariant(QVariantList{s.safe[0], s.safe[1], s.safe[2], s.safe[3]}));
}

void checkLayout(AcceptanceRunner& r, const Size& s, const QString& where)
{
    const QRectF safe = QRectF(s.safe[3], s.safe[0], r.window()->width() - s.safe[3] - s.safe[1],
                               r.window()->height() - s.safe[0] - s.safe[2])
                            .adjusted(-0.5, -0.5, 0.5, 0.5);
    QStringList outside;
    int seen = 0;
    for (const char* name : {"measureTabs", "measureClose", "measureReadout", "measureInstrumentArea", "scanBuild", "scanCount",
                             "scanPreviewCard", "scanSize", "scanBuilding", "scanBack"}) {
        QQuickItem* i = r.findItem(QString::fromLatin1(name));
        if (!i || !i->isVisible() || i->width() <= 0 || i->height() <= 0)
            continue;
        ++seen;
        const QRectF rect = i->mapRectToScene(QRectF(0, 0, i->width(), i->height()));
        if (!safe.contains(rect))
            outside << QStringLiteral("%1 %2,%3 %4x%5").arg(QString::fromLatin1(name)).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    }
    r.check(seen >= 3 && outside.isEmpty(), say(where + QStringLiteral(": inside the safe area")),
            outside.isEmpty() ? QStringLiteral("%1 parts").arg(seen) : outside.join(QStringLiteral("; ")));
}

// Renders photos of the box and writes them as PNG files named `<stem>_NN.png`.
// `scene` defaults to white A4 on a dark table; Vision's stand-in finds the
// paper when `vision`.
QStringList writePhotos(const StatePtr& st, const std::vector<synth::CameraSpec>& cameras, unsigned seed, const QString& stem,
                        synth::Scene scene = {}, bool vision = true)
{
    scene.object = synth::boxObject(kBox);
    scene.textureStrength = 0.2;
    const auto photos = synth::render(scene, cameras, seed);
    QStringList files;
    for (std::size_t i = 0; i < photos.size(); ++i) {
        const synth::Photo& p = photos[i];
        const QString key = QStringLiteral("%1_%2").arg(stem).arg(int(i), 2, 10, QLatin1Char('0'));
        const QString file = st->dir->filePath(key + QStringLiteral(".png"));
        QImage(p.rgba.data(), p.width, p.height, p.width * 4, QImage::Format_RGBA8888).save(file);
        ui::RectangleCorners c;
        for (std::size_t k = 0; k < 4; ++k)
            c[k] = QPointF(p.corners[k].x, p.corners[k].y);
        if (vision)
            st->corners.insert(key, c);
        st->truth.insert(key, c);
        st->masks.insert(key, QImage(p.mask.data(), p.width, p.height, p.width, QImage::Format_Grayscale8).copy());
        files << file;
    }
    return files;
}

QString keyOf(const StatePtr& st, const QString& file)
{
    for (auto it = st->masks.constBegin(); it != st->masks.constEnd(); ++it)
        if (file.contains(it.key()))
            return it.key();
    return {};
}

// Vision's rectangles and subject masks, from the renderer's truth; the
// camera and the library when `pickers`.
ui::MeasureDevices standIns(const StatePtr& st, bool pickers)
{
    ui::MeasureDevices d;
    d.detectRectangle = [st](const QString& file) -> std::optional<ui::RectangleCorners> {
        const QString key = keyOf(st, file);
        if (key.isEmpty() || !st->corners.contains(key))
            return std::nullopt;
        return st->corners.value(key);
    };
    d.subjectMask = [st](const QString& photo, const QString& maskFile) {
        const QString key = keyOf(st, photo);
        return !key.isEmpty() && st->masks.value(key).save(maskFile, "PNG");
    };
    if (pickers) {
        d.cameraAvailable = [] { return true; };
        auto copy = [st](const QString& folder) {
            const QString source = st->allRound.value(st->picked++ % st->allRound.size());
            QDir().mkpath(folder);
            const QString out = folder + QStringLiteral("/picked_") + QFileInfo(source).completeBaseName() + QStringLiteral(".jpg");
            ui::PhotoResult result;
            result.outcome = QImage(source).save(out, "JPG", 95) ? ui::PhotoResult::Outcome::Picked : ui::PhotoResult::Outcome::Failed;
            result.file = out;
            return result;
        };
        d.pickPhoto = [copy](QWindow*, ui::PhotoOrigin, const ui::CameraOptions&, const QString& folder, const QRectF&,
                             std::function<void(const ui::PhotoResult&)> finished) {
            const ui::PhotoResult result = copy(folder);
            QTimer::singleShot(0, QCoreApplication::instance(), [finished, result] { finished(result); });
        };
        d.pickPhotos = [copy, st](QWindow*, int maxCount, const QString& folder, const QRectF&,
                                  std::function<void(const std::vector<ui::PhotoResult>&)> finished) {
            std::vector<ui::PhotoResult> results;
            const int count = std::min<int>(maxCount, int(st->allRound.size()) - 1);
            for (int i = 0; i < count; ++i)
                results.push_back(copy(folder));
            QTimer::singleShot(0, QCoreApplication::instance(), [finished, results] { finished(results); });
        };
    }
    return d;
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<State>();
    Steps steps;

    steps.push_back([&r, st] {
        st->dir = std::make_unique<QTemporaryDir>();
        // As the engine's own unknown-lens test takes them (the focal length is
        // worked out from the papers: no EXIF here).
        st->allRound = writePhotos(st, synth::ring(12, 35, 55, 0, 360, 480, 28), 14, QStringLiteral("allround"));
        st->oneSide = writePhotos(st, synth::ring(6, 35, 50, -40, 110), 17, QStringLiteral("oneside"));
        synth::Scene pad;
        pad.paper = measure::legalReference();
        pad.paperColour = {0.98, 0.92, 0.52};
        pad.ruleSpacingMm = 8.7;
        pad.marginMm = 32;
        pad.table = {0.50, 0.33, 0.19};
        st->legal = writePhotos(st, synth::ring(12, 35, 55, 0, 360, 600, 28), 19, QStringLiteral("legal"), pad, false);
        r.check(st->allRound.size() == 12 && st->oneSide.size() == 6 && st->legal.size() == 12 && QFileInfo::exists(st->allRound.last()),
                say("the synthetic photos are written"));
        auto& m = measureOf(r);
        m.setDevices(standIns(st, false)); // a desktop: files, Vision stood in for
        m.scanClear();
        if (QQuickItem* screen = r.findItem(QStringLiteral("measureScreen"))) {
            QMetaObject::invokeMethod(screen, "close");
            QMetaObject::invokeMethod(screen, "resetForTest");
        }
        st->progressWatch = QObject::connect(&m, &ui::MeasureController::scanProgressChanged, &m, [st, &m] {
            if (m.scanBuilding() && m.scanProgress() > 0 && m.scanProgress() < 1)
                st->sawProgress = true;
            st->maxProgress = std::max(st->maxProgress, m.scanProgress());
        });
        st->finishedWatch = QObject::connect(&m, &ui::MeasureController::scanFinished, &m, [st](const QString& outcome) { st->lastOutcome = outcome; });
        applySize(r, kDesktop);
    });
    wait(steps, 2);

    // ---- the desktop: open the photos
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureButton")), say("open Measure")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("measureTab_scan")), say("click Scan")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        r.check(shown(r, QStringLiteral("scanIntro")) && shown(r, QStringLiteral("scanOpen")) && !shown(r, QStringLiteral("scanTake")),
                say("the how-to, with Open photos… (no camera here)"));
        checkLayout(r, kDesktop, QStringLiteral("desktop intro"));
        r.screenshot(QStringLiteral("measurescan_intro_desktop"));
        measureOf(r).setNextScanFiles(st->allRound);
        r.check(r.clickItem(QStringLiteral("scanOpen")), say("click Open photos…"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(60000), say("the photos are read")); });
    wait(steps, 2);
    steps.push_back([&r] {
        const QVariantList photos = measureOf(r).scanPhotos();
        int found = 0, masks = 0;
        for (const QVariant& p : photos) {
            found += p.toMap().value(QStringLiteral("source")).toString() == QStringLiteral("vision") ? 1 : 0;
            masks += p.toMap().value(QStringLiteral("mask")).toBool() ? 1 : 0;
        }
        r.check(photos.size() == 12 && found == 12 && masks == 12, say("twelve photos, each with its paper found and a subject mask"),
                QStringLiteral("%1 photos, %2 found, %3 masks").arg(photos.size()).arg(found).arg(masks));
        r.check(text(r, QStringLiteral("scanCount")) == QStringLiteral("12 photos") && shown(r, QStringLiteral("scanThumb_0")),
                say("the strip shows them"), text(r, QStringLiteral("scanCount")));
        checkLayout(r, kDesktop, QStringLiteral("desktop photos"));
        r.screenshot(QStringLiteral("measurescan_photos_desktop"));
        r.check(r.clickItem(QStringLiteral("scanThumb_2")), say("click the third photo"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(shown(r, QStringLiteral("scanReviewView")) && shown(r, QStringLiteral("scanCorner_0")), say("its corners to review"));
        st->corner = measureOf(r).scanPhotos().value(2).toMap().value(QStringLiteral("corners")).toList().value(0).toPointF();
        const QPointF from = center(r, QStringLiteral("scanCorner_0"));
        r.mousePress(from);
        r.mouseMove(from + QPointF(5, 4), Qt::LeftButton);
    });
    steps.push_back([&r] {
        const QPointF from = center(r, QStringLiteral("scanCorner_0"));
        r.mouseMove(from + QPointF(6, 5), Qt::LeftButton);
    });
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("scanLoupe")), say("the magnifier while a corner is dragged"));
        r.screenshot(QStringLiteral("measurescan_review_desktop"));
        r.mouseRelease(center(r, QStringLiteral("scanCorner_0")));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        const QVariantMap p = m.scanPhotos().value(2).toMap();
        const QPointF moved = p.value(QStringLiteral("corners")).toList().value(0).toPointF();
        r.check(QLineF(moved, st->corner).length() > 2 && p.value(QStringLiteral("source")).toString() == QStringLiteral("hand"),
                say("the corner follows the mouse, placed by hand"));
        m.scanSetCorner(2, 0, st->corner.x(), st->corner.y()); // back where Vision put it
        r.check(r.clickItem(QStringLiteral("scanReviewDone")), say("Done"));
    });
    wait(steps, 1);

    // ---- build
    steps.push_back([&r, st] {
        st->sawProgress = false;
        st->lastOutcome.clear();
        r.check(r.clickItem(QStringLiteral("scanBuild")), say("click Build"));
        r.check(measureOf(r).scanBuilding() && shown(r, QStringLiteral("scanBuilding")) && shown(r, QStringLiteral("scanCancel")),
                say("building: the progress card with Cancel"));
        r.screenshot(QStringLiteral("measurescan_building_desktop"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(180000), say("the build ends")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        const QVariantMap res = m.scanResult();
        r.check(st->lastOutcome == QStringLiteral("done") && res.value(QStringLiteral("valid")).toBool(), say("a result"), m.scanError());
        r.check(st->sawProgress, say("its progress was shown"), AcceptanceRunner::num(st->maxProgress));
        r.check(res.value(QStringLiteral("stereo")).toBool(), say("detail from the photos (stereo) was used"));
        st->w = res.value(QStringLiteral("width")).toDouble();
        st->d = res.value(QStringLiteral("depth")).toDouble();
        st->h = res.value(QStringLiteral("height")).toDouble();
        // The engine's tolerance with the lens unknown (tests/test_scan.cpp): 3.5 mm.
        r.check(std::abs(st->w - kBox.x) < 3.5 && std::abs(st->d - kBox.y) < 3.5 && std::abs(st->h - kBox.z) < 3.5,
                say("the box's size within the engine's tolerance (3.5 mm, the lens unknown)"),
                QStringLiteral("%1 x %2 x %3").arg(st->w).arg(st->d).arg(st->h));
        r.check(res.value(QStringLiteral("triangles")).toInt() > 100 && res.value(QStringLiteral("volume")).toDouble() > 0.85 * kBox.x * kBox.y * kBox.z,
                say("a closed mesh of about the box's volume"), AcceptanceRunner::num(res.value(QStringLiteral("volume")).toDouble()));
        r.check(shown(r, QStringLiteral("scanPreview")) && shown(r, QStringLiteral("scanSize")), say("the result: its preview and size"));
        r.check(text(r, QStringLiteral("measureReadoutCaption")).startsWith(QStringLiteral("Width")), say("the readout offers its width, depth, height"));
        checkLayout(r, kDesktop, QStringLiteral("desktop result"));
        r.screenshot(QStringLiteral("measurescan_result_desktop"));
        r.check(r.clickItem(QStringLiteral("measureAction_addToDesign")), say("click Add to design"));
    });
    wait(steps, 4);
    steps.push_back([&r, st] {
        r.check(!shown(r, QStringLiteral("measureScreen")), say("the workspace closes into the design"));
        r.check(r.app().bodyCount() == 1, say("one body"), QString::number(r.app().bodyCount()));
        if (r.app().bodyCount() == 1) {
            const auto bb = geom::boundingBox(r.body(0).shape());
            const double bw = bb.max.x - bb.min.x, bd = bb.max.y - bb.min.y, bh = bb.max.z - bb.min.z;
            r.check(std::abs(bw - st->w) < 0.5 && std::abs(bd - st->d) < 0.5 && std::abs(bh - st->h) < 0.5,
                    say("the body is the scan's size"), QStringLiteral("%1 x %2 x %3").arg(bw).arg(bd).arg(bh));
        }
        r.screenshot(QStringLiteral("measurescan_body_desktop"));
        r.app().undo();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().bodyCount() == 0, say("undo removes it"));
        r.check(r.clickItem(QStringLiteral("measureButton")), say("back to Measure"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(current(r) == QStringLiteral("scan") && shown(r, QStringLiteral("scanPreview")), say("Scan, its result still there"));
        r.check(r.clickItem(QStringLiteral("scanBack")), say("back to the photos"));
    });
    wait(steps, 1);

    // ---- cancel
    steps.push_back([&r, st] {
        st->lastOutcome.clear();
        r.check(r.clickItem(QStringLiteral("scanBuild")), say("cancel: Build"));
        r.check(r.clickItem(QStringLiteral("scanCancel")), say("cancel: Cancel at once"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(60000), say("cancel: the build stops")); });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(st->lastOutcome == QStringLiteral("cancelled") && !m.scanResult().value(QStringLiteral("valid")).toBool()
                    && m.scanPhotos().size() == 12 && shown(r, QStringLiteral("scanBuild")),
                say("cancel: nothing built, the photos kept"), st->lastOutcome);
    });

    // ---- photos from one side
    steps.push_back([&r, st] {
        measureOf(r).scanClear();
        measureOf(r).setNextScanFiles(st->oneSide);
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("scanOpen")), say("one side: open six photos")); });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(60000), say("one side: read")); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("scanBuild")), say("one side: Build")); });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(180000), say("one side: built")); });
    wait(steps, 2);
    steps.push_back([&r] {
        const QVariantMap res = measureOf(r).scanResult();
        bool gap = false;
        for (const QVariant& w : res.value(QStringLiteral("warnings")).toList())
            gap = gap || w.toString().contains(QStringLiteral("gap of"));
        r.check(res.value(QStringLiteral("valid")).toBool() && gap, say("one side: a warning about the gap"),
                res.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral(" | ")));
        r.check(shown(r, QStringLiteral("scanWarning_0")), say("one side: the warning is shown"));
        r.screenshot(QStringLiteral("measurescan_warning_desktop"));
        r.check(r.clickItem(QStringLiteral("measureClose")), say("close Measure"));
    });

    // ---- the phone: the camera and the library
    steps.push_back([&r, st] {
        measureOf(r).scanClear();
        measureOf(r).setDevices(standIns(st, true));
        applySize(r, kPhone);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("phone: open Measure")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(current(r) == QStringLiteral("scan"), say("phone: on Scan"), current(r));
        r.check(shown(r, QStringLiteral("scanTake")) && shown(r, QStringLiteral("scanChoose")), say("phone: Take photos and Choose photos"));
        checkLayout(r, kPhone, QStringLiteral("phone intro"));
        r.screenshot(QStringLiteral("measurescan_intro_phone"));
        r.check(r.tapItem(QStringLiteral("scanTake")), say("phone: Take photos"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(30000), say("phone: the photo is read")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(measureOf(r).scanPhotos().size() == 1 && shown(r, QStringLiteral("scanTakeAnother")), say("phone: one photo, Take another offered"));
        r.check(r.tapItem(QStringLiteral("scanChooseMore")), say("phone: choose more from the library"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(60000), say("phone: read")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(measureOf(r).scanPhotos().size() == 12, say("phone: twelve photos"), QString::number(measureOf(r).scanPhotos().size()));
        checkLayout(r, kPhone, QStringLiteral("phone photos"));
        r.screenshot(QStringLiteral("measurescan_photos_phone"));
        r.check(r.tapItem(QStringLiteral("scanBuild")), say("phone: Build"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(180000), say("phone: built")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(measureOf(r).scanResult().value(QStringLiteral("valid")).toBool(), say("phone: a result"));
        checkLayout(r, kPhone, QStringLiteral("phone result"));
        r.screenshot(QStringLiteral("measurescan_result_phone"));
    });

    // ---- the iPhone Duo, both displays: the result and the photos
    for (const Size* s : {&kDuoOuter, &kDuoInner}) {
        steps.push_back([&r, s] { applySize(r, *s); });
        wait(steps, 3);
        steps.push_back([&r, s] {
            checkLayout(r, *s, s->name + QStringLiteral(" result"));
            r.screenshot(QStringLiteral("measurescan_result_") + s->name);
            r.check(press(r, *s, QStringLiteral("scanBack")), say(s->name + QStringLiteral(": back to the photos")));
        });
        wait(steps, 2);
        steps.push_back([&r, s] {
            checkLayout(r, *s, s->name + QStringLiteral(" photos"));
            r.screenshot(QStringLiteral("measurescan_photos_") + s->name);
            r.check(press(r, *s, QStringLiteral("scanBuild")), say(s->name + QStringLiteral(": Build")));
        });
        steps.push_back([&r] { r.check(measureOf(r).waitForScan(180000), say("built again")); });
        wait(steps, 2);
    }

    // ---- Legal: a yellow lined pad, found by FoldForm's own paper finder
    steps.push_back([&r, st] {
        measureOf(r).scanClear();
        measureOf(r).setDevices(standIns(st, false));
        applySize(r, kDesktop);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("scanIntro")) && shown(r, QStringLiteral("scanIntroTips")), say("legal: the how-to, with what works best"),
                text(r, QStringLiteral("scanIntroTips")));
        r.check(r.clickItem(QStringLiteral("scanPaper_legal")), say("legal: pick Legal paper"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        r.check(measureOf(r).scanPaper() == QStringLiteral("legal") && text(r, QStringLiteral("scanIntro")).contains(QStringLiteral("Legal")),
                say("legal: picked, and the how-to says so"));
        checkLayout(r, kDesktop, QStringLiteral("legal intro"));
        r.screenshot(QStringLiteral("measurescan_intro_legal_desktop"));
        measureOf(r).setNextScanFiles(st->legal);
        r.check(r.clickItem(QStringLiteral("scanOpen")), say("legal: open twelve photos of the pad"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(60000), say("legal: read")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        const QVariantList photos = measureOf(r).scanPhotos();
        int detected = 0;
        double worst = 0;
        for (const QVariant& v : photos) {
            const QVariantMap p = v.toMap();
            if (p.value(QStringLiteral("source")).toString() != QStringLiteral("detected"))
                continue;
            ++detected;
            const ui::RectangleCorners truth = st->truth.value(keyOf(st, p.value(QStringLiteral("url")).toUrl().toLocalFile()));
            for (const QVariant& c : p.value(QStringLiteral("corners")).toList()) {
                double nearest = 1e9;
                for (const QPointF& t : truth)
                    nearest = std::min(nearest, QLineF(c.toPointF(), t).length());
                worst = std::max(worst, nearest);
            }
        }
        r.check(photos.size() == 12 && detected == 12 && worst < 4, say("legal: the paper finder found every pad (corners within 4 px)"),
                QStringLiteral("%1 photos, %2 found, worst corner %3 px").arg(photos.size()).arg(detected).arg(AcceptanceRunner::num(worst)));
        r.screenshot(QStringLiteral("measurescan_photos_legal_desktop"));
        r.check(r.clickItem(QStringLiteral("scanBuild")), say("legal: Build"));
    });
    steps.push_back([&r] { r.check(measureOf(r).waitForScan(180000), say("legal: built")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        const QVariantMap res = measureOf(r).scanResult();
        const double w = res.value(QStringLiteral("width")).toDouble(), d = res.value(QStringLiteral("depth")).toDouble(),
                     h = res.value(QStringLiteral("height")).toDouble();
        // The same box from the same ring (a little farther: the sheet is
        // longer) as on A4: the hull's bulge on its flat sides is the same, the
        // paper adds nothing.
        r.check(res.value(QStringLiteral("valid")).toBool() && std::abs(w - st->w) < 2 && std::abs(d - st->d) < 2 && std::abs(h - st->h) < 2
                    && std::abs(w - kBox.x) < 5 && std::abs(d - kBox.y) < 5 && std::abs(h - kBox.z) < 5,
                say("legal: the box's size as on A4 (within 2 mm), and within 5 mm of the truth"),
                QStringLiteral("%1 x %2 x %3 (on A4 %4 x %5 x %6)").arg(w).arg(d).arg(h).arg(st->w).arg(st->d).arg(st->h));
        checkLayout(r, kDesktop, QStringLiteral("legal result"));
        r.screenshot(QStringLiteral("measurescan_result_legal_desktop"));
    });

    // ---- clean up for the next scenarios
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        m.setScanPaper(QStringLiteral("a4"));
        if (QQuickItem* screen = r.findItem(QStringLiteral("measureScreen")))
            QMetaObject::invokeMethod(screen, "close");
        QObject::disconnect(st->progressWatch);
        QObject::disconnect(st->finishedWatch);
        m.scanClear();
        m.setDevices(ui::platformMeasureDevices());
        st->dir.reset();
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measurescan"), 113, steps});

} // namespace
} // namespace os::app
