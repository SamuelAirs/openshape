// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Measure's camera screen (docs/MEASURE.md, Photo; src/ui/ios/CameraScreenIos.mm)
// as the Qt side sees it: what Take photo asks the camera for
// (MeasureDevices::pickPhoto's CameraOptions), with a stand-in camera that
// records each call. Photo's first Take photo offers no overlay; the second
// offers the first photo (kept on disk although the first was cleared), and
// the first is removed once the second replaces it. Scan's Take photo asks
// for the Scan angle and no overlay; the library is asked with the defaults.
// The camera screen itself is native and runs only on a device (TD-180).

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <memory>
#include <vector>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

struct Call {
    ui::PhotoOrigin origin = ui::PhotoOrigin::Camera;
    ui::CameraOptions camera;
    QString file; // what the stand-in handed back ("": nothing)
};

struct State {
    std::unique_ptr<QTemporaryDir> dir;
    QString photo;
    std::vector<Call> calls;
};
using StatePtr = std::shared_ptr<State>;

QString say(const QString& text) { return QStringLiteral("measurecamerascreen: ") + text; }

ui::MeasureController& measureOf(AcceptanceRunner& r) { return *r.app().measure(); }

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* i = r.findItem(name);
    return i && i->isVisible();
}

QString stage(AcceptanceRunner& r)
{
    QQuickItem* i = r.findItem(QStringLiteral("measurePhoto"));
    return i ? i->property("stage").toString() : QString();
}

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

QString purposeName(ui::CameraOptions::Purpose purpose)
{
    return purpose == ui::CameraOptions::Purpose::Scan ? QStringLiteral("scan") : QStringLiteral("photo");
}

QString describe(const Call& c)
{
    return QStringLiteral("%1, %2, overlay \"%3\"")
        .arg(c.origin == ui::PhotoOrigin::Camera ? QStringLiteral("camera") : QStringLiteral("library"), purposeName(c.camera.purpose),
             QFileInfo(c.camera.overlayFile).fileName());
}

// A photo-sized picture of a card on a table (the corners are placed by
// hand: no rectangle finder here).
bool makePhoto(const StatePtr& st)
{
    QImage img(1600, 1200, QImage::Format_RGB32);
    img.fill(QColor(0x8A, 0x6E, 0x50));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xF4, 0xF1, 0xEA));
    p.drawRoundedRect(QRectF(500, 400, 504, 352), 20, 20);
    p.end();
    st->photo = st->dir->filePath(QStringLiteral("table.png"));
    return img.save(st->photo);
}

// A camera that records what it is asked: a photo for Photo and the
// library, Cancel for Scan (Scan's reading of a photo is the measurescan
// scenario's).
ui::MeasureDevices standIns(const StatePtr& st)
{
    ui::MeasureDevices d;
    d.cameraAvailable = [] { return true; };
    d.pickPhoto = [st](QWindow*, ui::PhotoOrigin origin, const ui::CameraOptions& camera, const QString& folder, const QRectF&,
                       std::function<void(const ui::PhotoResult&)> finished) {
        Call call;
        call.origin = origin;
        call.camera = camera;
        ui::PhotoResult result;
        result.outcome = ui::PhotoResult::Outcome::Cancelled;
        if (origin == ui::PhotoOrigin::Library || camera.purpose == ui::CameraOptions::Purpose::Photo) {
            QDir().mkpath(folder);
            const QString out = folder + QStringLiteral("/camerascreen_%1.jpg").arg(st->calls.size() + 1);
            const bool saved = QImage(st->photo).save(out, "JPG", 90);
            result.outcome = saved ? ui::PhotoResult::Outcome::Picked : ui::PhotoResult::Outcome::Failed;
            result.file = out;
            call.file = saved ? out : QString();
        }
        st->calls.push_back(call);
        QTimer::singleShot(0, QCoreApplication::instance(), [finished, result] { finished(result); });
    };
    return d;
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<State>();
    Steps steps;

    steps.push_back([&r, st] {
        st->dir = std::make_unique<QTemporaryDir>();
        r.check(st->dir->isValid() && makePhoto(st), say("a photo for the stand-in camera"));
        auto& m = measureOf(r);
        m.clearPhoto();
        m.scanClear();
        m.setDevices(standIns(st));
        if (QQuickItem* screen = r.findItem(QStringLiteral("measureScreen"))) {
            QMetaObject::invokeMethod(screen, "close");
            QMetaObject::invokeMethod(screen, "resetForTest");
        }
        r.app().setTouchMode(true);
        r.resizeWindow(402, 874);
        r.window()->setProperty("simulatedSafeArea", QVariant(QVariantList{62, 0, 34, 0}));
    });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("open Measure")); });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_photo")), say("tap Photo")); });
    wait(steps, 1);

    // ---- Photo: the first Take photo, no overlay
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("photoTake")), say("Take photo is offered"));
        r.check(r.tapItem(QStringLiteral("photoTake")), say("tap Take photo"));
    });
    steps.push_back([&r, st] {
        r.check(measureOf(r).waitForPhoto(15000), say("the first photo is read"));
        const bool one = st->calls.size() == 1;
        r.check(one && st->calls[0].origin == ui::PhotoOrigin::Camera && st->calls[0].camera.purpose == ui::CameraOptions::Purpose::Photo,
                say("the camera is asked for a photo from above"), one ? describe(st->calls[0]) : QString::number(st->calls.size()));
        r.check(one && st->calls[0].camera.overlayFile.isEmpty(), say("the first photo has no overlay"));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(stage(r) == QStringLiteral("corners"), say("the corners step"), stage(r));
        // New photo: the photo measured is dropped; the camera's file stays
        // for the next one's overlay.
        measureOf(r).clearPhoto();
        r.check(!st->calls.empty() && QFileInfo::exists(st->calls[0].file), say("the first photo is kept for the overlay after New photo"));
    });
    wait(steps, 2);

    // ---- Photo: the second Take photo offers the first as the overlay
    steps.push_back([&r] {
        r.check(stage(r) == QStringLiteral("start") && shown(r, QStringLiteral("photoTake")), say("back at Take photo"), stage(r));
        r.check(r.tapItem(QStringLiteral("photoTake")), say("tap Take photo again"));
    });
    steps.push_back([&r, st] {
        r.check(measureOf(r).waitForPhoto(15000), say("the second photo is read"));
        const bool two = st->calls.size() == 2;
        r.check(two && st->calls[1].camera.purpose == ui::CameraOptions::Purpose::Photo && !st->calls[0].file.isEmpty()
                    && QFileInfo(st->calls[1].camera.overlayFile) == QFileInfo(st->calls[0].file),
                say("the second photo offers the first as its overlay"), two ? describe(st->calls[1]) : QString::number(st->calls.size()));
    });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(st->calls.size() == 2 && !QFileInfo::exists(st->calls[0].file) && QFileInfo::exists(st->calls[1].file),
                say("the first photo is removed once the second replaces it"));
        r.check(stage(r) == QStringLiteral("corners"), say("the second photo is measured"), stage(r));
        measureOf(r).clearPhoto();
    });
    wait(steps, 2);

    // ---- the library: the default options
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("photoChoose")), say("tap Choose photo")); });
    steps.push_back([&r, st] {
        r.check(measureOf(r).waitForPhoto(15000), say("the chosen photo is read"));
        const bool three = st->calls.size() == 3;
        r.check(three && st->calls[2].origin == ui::PhotoOrigin::Library && st->calls[2].camera.overlayFile.isEmpty(),
                say("the library is asked without an overlay"), three ? describe(st->calls[2]) : QString::number(st->calls.size()));
        r.check(three && QFileInfo::exists(st->calls[1].file), say("a chosen photo leaves the camera's last photo for the overlay"));
        measureOf(r).clearPhoto();
    });
    wait(steps, 2);

    // ---- Scan: the angle band, no overlay
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureTab_scan")), say("tap Scan")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("scanTake")), say("Scan: Take photos is offered"));
        r.check(r.tapItem(QStringLiteral("scanTake")), say("Scan: tap Take photos"));
    });
    wait(steps, 3);
    steps.push_back([&r, st] {
        const bool four = st->calls.size() == 4;
        r.check(four && st->calls[3].origin == ui::PhotoOrigin::Camera && st->calls[3].camera.purpose == ui::CameraOptions::Purpose::Scan,
                say("Scan asks the camera for the Scan angle"), four ? describe(st->calls[3]) : QString::number(st->calls.size()));
        r.check(four && st->calls[3].camera.overlayFile.isEmpty(), say("Scan offers no overlay"));
        r.check(measureOf(r).scanPhotos().isEmpty() && measureOf(r).scanError().isEmpty(), say("Scan: Cancel leaves no photo and no error"));
    });

    // ---- clean up for the next scenarios
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        if (QQuickItem* screen = r.findItem(QStringLiteral("measureScreen")))
            QMetaObject::invokeMethod(screen, "close");
        m.scanClear();
        m.clearPhoto();
        m.setDevices(ui::platformMeasureDevices());
        r.check(st->calls.size() == 4 && !QFileInfo::exists(st->calls[1].file), say("another camera: the last photo is removed"));
        r.window()->setProperty("simulatedSafeArea", QVariant());
        st->dir.reset();
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measurecamerascreen"), 114, steps});

} // namespace
} // namespace os::app
