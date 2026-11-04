// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Level's calibration and check, end for end (docs/MEASURE.md, "Level:
// calibrating and checking"), with gravity the run makes: a sensor 0.6° off
// lying on a surface sloping 1.5°. Calibrate by tapping through both steps
// (the hold makes each reading; Turned starts the second), the offset found
// within 0.05° and kept, a level surface then reads 0.0° (also with the
// window turned to landscape: the offset belongs to the device); Check
// reports the two readings agree; standing on an edge calibrated apart (0.4°
// on a 1.5° shelf); Reset, and Check reports the 0.6° again. The steps and
// the calibrated line at phone, iPhone Duo outer / inner / turned, iPad and
// desktop sizes, inside the safe area with nothing over the bubble:
// pictures measurelevelcal_<stage>_<size>.png.

#include "app/AcceptanceRunner.h"
#include "core/Math.h"
#include "measure/Level.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QSettings>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

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

// The simulated sensor and surfaces.
const Vec2 kOffset{0.6, 0.0};   // the sensor, lying (degrees about each axis)
const Vec2 kSlope{1.2, 0.9};    // the surface under the phone: 1.5° in all
constexpr double kEdgeOffset = 0.4, kShelf = 1.5;

struct State {
    std::function<void(const ui::GravityReading&)> gravity;
    double clock = 0;
    int haptics = 0;
};
using StatePtr = std::shared_ptr<State>;

QString say(const QString& text) { return QStringLiteral("measurelevelcal: ") + text; }
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
QRectF sceneRect(QQuickItem* i) { return i ? i->mapRectToScene(QRectF(0, 0, i->width(), i->height())) : QRectF(); }
bool press(AcceptanceRunner& r, const Size& size, const QString& name) { return size.touch ? r.tapItem(name) : r.clickItem(name); }
void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

void applySize(AcceptanceRunner& r, const Size& s)
{
    r.app().setTouchMode(s.touch);
    r.resizeWindow(s.width, s.height);
    const bool none = s.safe[0] == 0 && s.safe[1] == 0 && s.safe[2] == 0 && s.safe[3] == 0;
    r.window()->setProperty("simulatedSafeArea", none ? QVariant() : QVariant(QVariantList{s.safe[0], s.safe[1], s.safe[2], s.safe[3]}));
}

// A sensor reading `offset` off the truth (a small turn about each axis).
measure::Gravity misread(const measure::Gravity& truth, Vec2 offsetDeg)
{
    const double ax = offsetDeg.x * kPi / 180, ay = offsetDeg.y * kPi / 180;
    const double x1 = truth.x * std::cos(ax) - truth.z * std::sin(ax);
    const double z1 = truth.x * std::sin(ax) + truth.z * std::cos(ax);
    return {x1, truth.y * std::cos(ay) - z1 * std::sin(ay), truth.y * std::sin(ay) + z1 * std::cos(ay)};
}
// Lying on the sloping surface, first way round or turned half a turn.
measure::Gravity lying(bool turned) { return misread(measure::fromLyingAngles(turned ? kSlope * -1.0 : kSlope), kOffset); }
measure::Gravity edge(double deg)
{
    const double a = deg * kPi / 180;
    return {-std::sin(a), std::cos(a), 0};
}

// Twenty-five readings a tenth of a second apart: held after the first second.
void push(const StatePtr& st, const measure::Gravity& g, int turn = 0, int count = 25)
{
    for (int i = 0; i < count && st->gravity; ++i) {
        st->clock += 0.1;
        st->gravity({g.x, g.y, g.z, turn});
    }
}

void forgetCalibration()
{
    QSettings settings;
    settings.remove(QStringLiteral("measure/levelOffset"));
}

// The level's parts inside the safe area, and nothing of its footer over the bubble.
void checkLayout(AcceptanceRunner& r, const Size& s, const QString& where)
{
    const QRectF safe = QRectF(s.safe[3], s.safe[0], r.window()->width() - s.safe[3] - s.safe[1], r.window()->height() - s.safe[0] - s.safe[2])
                            .adjusted(-0.5, -0.5, 0.5, 0.5);
    QStringList outside;
    int seen = 0;
    QRectF footerTop;
    for (const char* name : {"levelValue", "levelHold", "levelCalText", "levelCalTurned", "levelCalCancel", "levelCalDone", "levelKeepReference",
                             "levelCalibrate", "levelCheck", "levelCalibratedText", "levelCalReset", "levelHoldHint", "levelBubbleVial"}) {
        QQuickItem* i = item(r, QString::fromLatin1(name));
        if (!i || !i->isVisible() || i->width() <= 0 || i->height() <= 0)
            continue;
        ++seen;
        const QRectF rect = sceneRect(i);
        if (!safe.contains(rect))
            outside << QStringLiteral("%1 %2,%3 %4x%5").arg(QString::fromLatin1(name)).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
        if (QByteArray(name) == "levelHold")
            footerTop = rect;
    }
    r.check(seen >= 5 && outside.isEmpty(), say(where + QStringLiteral(": inside the safe area")),
            outside.isEmpty() ? QStringLiteral("%1 parts").arg(seen) : outside.join(QStringLiteral("; ")));
    QQuickItem* vial = item(r, QStringLiteral("levelBubbleVial"));
    QQuickItem* number = item(r, QStringLiteral("levelValue"));
    const QRectF v = sceneRect(vial);
    r.check(vial && vial->isVisible() && v.width() >= 63 && v.bottom() <= footerTop.top() + 0.5 && v.top() >= sceneRect(number).bottom() - 0.5,
            say(where + QStringLiteral(": the bubble between the number and the footer")),
            QStringLiteral("vial %1..%2 (%3 wide), footer from %4").arg(v.top()).arg(v.bottom()).arg(v.width()).arg(footerTop.top()));
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<State>();
    Steps steps;

    steps.push_back([&r, st] {
        forgetCalibration();
        auto& m = measureOf(r);
        ui::MeasureDevices d;
        d.watchGravity = [st](QWindow*, std::function<void(const ui::GravityReading&)> handler) -> ui::StopWatching {
            st->gravity = std::move(handler);
            return [st] { st->gravity = nullptr; };
        };
        d.haptic = [st](int) { ++st->haptics; };
        m.setDevices(d);
        m.setClock([st] { return st->clock; });
        if (QQuickItem* screen = item(r, QStringLiteral("measureScreen"))) {
            QMetaObject::invokeMethod(screen, "close");
            QMetaObject::invokeMethod(screen, "resetForTest");
        }
        applySize(r, kPhone);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("measureButton")), say("open Measure")); });
    wait(steps, 2);
    steps.push_back([&r, st] {
        r.check(bool(st->gravity), say("gravity is watched"));
        push(st, lying(false), 0, 3);
        r.check(r.tapItem(QStringLiteral("measureTab_level")), say("tap Level"));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.gravityAvailable() && !m.levelCalibrated() && shown(r, QStringLiteral("levelCalibrate")) && shown(r, QStringLiteral("levelCheck"))
                    && !shown(r, QStringLiteral("levelCalReset")),
                say("not calibrated: Calibrate and Check offered"));
        r.check(r.tapItem(QStringLiteral("levelCalibrate")), say("tap Calibrate"));
    });
    wait(steps, 1);
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(m.levelCalMode() == QStringLiteral("calibrate") && m.levelCalStep() == 1 && text(r, QStringLiteral("levelCalText")).contains(QStringLiteral("1 of 2")),
                say("calibrate: step 1 of 2"), text(r, QStringLiteral("levelCalText")));
        r.check(!shown(r, QStringLiteral("levelKeepReference")) && !shown(r, QStringLiteral("levelHoldHint")) && shown(r, QStringLiteral("levelCalCancel")),
                say("calibrate: the step replaces the usual controls"));
        r.screenshot(QStringLiteral("measurelevelcal_step1_phone"));
        push(st, lying(false));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        const double reads = m.levelSlope();
        r.check(m.levelCalStep() == 2 && shown(r, QStringLiteral("levelCalTurned")) && text(r, QStringLiteral("levelCalText")).contains(QStringLiteral("2 of 2")),
                say("calibrate: held, then step 2 (turn it)"), QStringLiteral("it read %1°").arg(AcceptanceRunner::num(reads)));
        r.screenshot(QStringLiteral("measurelevelcal_step2_phone"));
        r.check(r.tapItem(QStringLiteral("levelCalTurned")), say("calibrate: tap Turned"));
    });
    steps.push_back([st] { push(st, lying(true)); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.levelCalStep() == 3 && std::abs(m.levelCalFound() - kOffset.length()) < 0.05, say("calibrate: the 0.6° offset found"),
                AcceptanceRunner::num(m.levelCalFound()) + QStringLiteral(": ") + m.levelCalResult());
        r.check(m.levelCalResult().contains(QStringLiteral("sloped 1.5°")), say("calibrate: the surface's 1.5° slope told"), m.levelCalResult());
        QSettings settings;
        bool kept = false;
        for (const QString& key : settings.allKeys())
            kept = kept || (key.startsWith(QStringLiteral("measure/levelOffset/")) && key.endsWith(QStringLiteral("/lyingX")));
        r.check(kept, say("calibrate: kept"));
        r.screenshot(QStringLiteral("measurelevelcal_result_phone"));
        r.check(r.tapItem(QStringLiteral("levelCalDone")), say("calibrate: tap Done"));
    });
    // A level surface, read by the same sensor: 0.0 once corrected.
    steps.push_back([st] { push(st, misread({0, 0, -1}, kOffset)); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.levelCalMode().isEmpty() && m.levelHasHold() && text(r, QStringLiteral("levelValue")) == QStringLiteral("0.0"),
                say("a level surface reads 0.0°"), text(r, QStringLiteral("levelValue")) + QStringLiteral(" (slope %1)").arg(AcceptanceRunner::num(m.levelSlope())));
        r.check(m.levelCalibrated() && text(r, QStringLiteral("levelCalibratedText")).startsWith(QStringLiteral("Calibrated: 0.6°")) && shown(r, QStringLiteral("levelCalReset")),
                say("the calibrated line, with Reset"), text(r, QStringLiteral("levelCalibratedText")));
        r.check(!shown(r, QStringLiteral("levelCalibrate")) && shown(r, QStringLiteral("levelCheck")), say("calibrated: Reset in Calibrate's place, Check still there"));
        r.screenshot(QStringLiteral("measurelevelcal_calibrated_phone"));
    });
    // The window turned to landscape: the device's offset turns with it.
    steps.push_back([st] {
        const measure::Gravity upright = misread({0, 0, -1}, kOffset);
        push(st, measure::uprightToWindow(upright, 1), 1);
    });
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.levelSlope() < 0.02, say("landscape: a level surface still reads level"), AcceptanceRunner::num(m.levelSlope()));
    });
    steps.push_back([st] { push(st, misread({0, 0, -1}, kOffset), 0, 3); });

    // ---- Check: the two readings agree now
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("levelCheck")), say("tap Check")); });
    steps.push_back([&r, st] {
        r.check(measureOf(r).levelCalMode() == QStringLiteral("check") && text(r, QStringLiteral("levelCalText")).contains(QStringLiteral("Check · 1 of 2")),
                say("check: step 1"), text(r, QStringLiteral("levelCalText")));
        push(st, lying(false));
    });
    steps.push_back([&r] { r.check(measureOf(r).levelCalStep() == 2 && r.tapItem(QStringLiteral("levelCalTurned")), say("check: held; tap Turned")); });
    steps.push_back([st] { push(st, lying(true)); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.levelCalStep() == 3 && m.levelCalFound() < 0.05 && m.levelCalResult().contains(QStringLiteral("reads true")),
                say("check: the readings agree (it reads true)"), m.levelCalResult());
        r.screenshot(QStringLiteral("measurelevelcal_checked_phone"));
        r.check(r.tapItem(QStringLiteral("levelCalDone")), say("check: Done"));
    });

    // ---- standing on an edge: calibrated apart
    steps.push_back([&r, st] {
        push(st, edge(kShelf + kEdgeOffset), 0, 3);
        r.check(!measureOf(r).levelCalibrated() && !measureOf(r).levelLying(), say("standing: not calibrated (apart from lying)"));
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("levelCalibrate")), say("standing: tap Calibrate")); });
    steps.push_back([st] { push(st, edge(kShelf + kEdgeOffset)); });
    steps.push_back([&r] { r.check(measureOf(r).levelCalStep() == 2 && r.tapItem(QStringLiteral("levelCalTurned")), say("standing: held; tap Turned")); });
    steps.push_back([st] { push(st, edge(-kShelf + kEdgeOffset)); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.levelCalStep() == 3 && std::abs(m.levelCalFound() - kEdgeOffset) < 0.05, say("standing: the 0.4° offset found"),
                AcceptanceRunner::num(m.levelCalFound()) + QStringLiteral(": ") + m.levelCalResult());
        r.check(r.tapItem(QStringLiteral("levelCalDone")), say("standing: Done"));
    });
    steps.push_back([st] { push(st, edge(kEdgeOffset)); });
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(std::abs(m.levelEdge()) < 0.02 && m.levelCalibrated(), say("standing: a level shelf reads 0.0°"), AcceptanceRunner::num(m.levelEdge()));
    });

    // ---- Reset (lying), and Check reports the offset again
    steps.push_back([st] { push(st, misread({0, 0, -1}, kOffset)); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("levelCalReset")), say("tap Reset")); });
    steps.push_back([&r, st] {
        auto& m = measureOf(r);
        r.check(!m.levelCalibrated() && !shown(r, QStringLiteral("levelCalReset")) && shown(r, QStringLiteral("levelCalibrate")) && m.levelSlope() > 0.55,
                say("reset: the raw reading again, Calibrate back"),
                AcceptanceRunner::num(m.levelSlope()));
        push(st, edge(kEdgeOffset), 0, 3);
        r.check(m.levelCalibrated(), say("reset: standing keeps its own"));
        push(st, misread({0, 0, -1}, kOffset), 0, 3);
        r.check(r.tapItem(QStringLiteral("levelCheck")), say("reset: tap Check"));
    });
    steps.push_back([st] { push(st, lying(false)); });
    steps.push_back([&r] { r.check(measureOf(r).levelCalStep() == 2 && r.tapItem(QStringLiteral("levelCalTurned")), say("reset: check held; tap Turned")); });
    steps.push_back([st] { push(st, lying(true)); });
    wait(steps, 1);
    steps.push_back([&r] {
        auto& m = measureOf(r);
        r.check(m.levelCalStep() == 3 && std::abs(m.levelCalFound() - kOffset.length()) < 0.05 && m.levelCalResult().contains(QStringLiteral("off by 0.6°")),
                say("reset: Check reports the 0.6° the two readings disagree by"), m.levelCalResult());
        r.check(r.tapItem(QStringLiteral("levelCalDone")), say("reset: Done"));
        r.check(r.tapItem(QStringLiteral("measureClose")), say("close"));
    });
    wait(steps, 1);

    // ---- every size: step 2 and the calibrated line
    for (const Size* s : {&kPhone, &kDuoOuter, &kDuoInner, &kDuoTurned, &kIpad, &kDesktop}) {
        steps.push_back([&r, s] { applySize(r, *s); });
        wait(steps, 3);
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("measureButton")), say(s->name + QStringLiteral(": open"))); });
        wait(steps, 2);
        steps.push_back([&r, s, st] {
            push(st, misread({0, 0, -1}, kOffset), 0, 3);
            r.check(press(r, *s, QStringLiteral("measureTab_level")), say(s->name + QStringLiteral(": Level")));
        });
        wait(steps, 1);
        // Calibrated before: Reset first (it stands in Calibrate's place).
        steps.push_back([&r, s] {
            if (shown(r, QStringLiteral("levelCalReset")))
                r.check(press(r, *s, QStringLiteral("levelCalReset")), say(s->name + QStringLiteral(": Reset")));
        });
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("levelCalibrate")), say(s->name + QStringLiteral(": Calibrate"))); });
        steps.push_back([st] { push(st, lying(false)); });
        wait(steps, 2);
        steps.push_back([&r, s] {
            r.check(measureOf(r).levelCalStep() == 2, say(s->name + QStringLiteral(": step 2")));
            checkLayout(r, *s, s->name + QStringLiteral(" step 2"));
            r.screenshot(QStringLiteral("measurelevelcal_step2_%1").arg(s->name));
        });
        steps.push_back([&r, s] { r.check(press(r, *s, QStringLiteral("levelCalTurned")), say(s->name + QStringLiteral(": Turned"))); });
        steps.push_back([st] { push(st, lying(true)); });
        wait(steps, 1);
        steps.push_back([&r, s] {
            r.check(measureOf(r).levelCalStep() == 3 && std::abs(measureOf(r).levelCalFound() - kOffset.length()) < 0.05, say(s->name + QStringLiteral(": calibrated")));
            checkLayout(r, *s, s->name + QStringLiteral(" result"));
            r.screenshot(QStringLiteral("measurelevelcal_result_%1").arg(s->name));
            r.check(press(r, *s, QStringLiteral("levelCalDone")), say(s->name + QStringLiteral(": Done")));
        });
        steps.push_back([st] { push(st, misread({0, 0, -1}, kOffset)); });
        wait(steps, 2);
        steps.push_back([&r, s] {
            r.check(shown(r, QStringLiteral("levelCalReset")), say(s->name + QStringLiteral(": the calibrated line")));
            checkLayout(r, *s, s->name + QStringLiteral(" calibrated"));
            r.screenshot(QStringLiteral("measurelevelcal_calibrated_%1").arg(s->name));
            r.check(press(r, *s, QStringLiteral("measureClose")), say(s->name + QStringLiteral(": close")));
        });
        wait(steps, 1);
    }

    // ---- clean up for the next scenarios
    steps.push_back([&r] {
        forgetCalibration();
        auto& m = measureOf(r);
        m.setDevices(ui::platformMeasureDevices());
        m.setClock(nullptr);
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measurelevelcal"), 114, steps});

} // namespace
} // namespace os::app
