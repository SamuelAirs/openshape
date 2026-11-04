// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Level's calibration and check, end for end (docs/MEASURE.md, "Level:
// calibrating and checking"): two held readings on any steady surface, the
// second with the phone turned half a turn in the same spot. Calibrate keeps
// half their sum as the sensor's offset (QSettings, per screen; lying and
// standing apart, standing per edge) and takes it off every later reading;
// Check reports how far the two disagree with the offset already taken off.

#include "core/Log.h"
#include "ui/MeasureController.h"

#include <QtCore/QSettings>

#include <cmath>

namespace os::ui {

namespace {

const char* const kLevelKey = "measure/levelOffset/";
// More than this is not a sensor's offset: the phone moved, or was not
// turned in the same spot.
constexpr double kMostOffsetDeg = 5.0;
// A check within this reads true.
constexpr double kTrueWithinDeg = 0.1;

QString deg(double v) { return QString::number(std::abs(v), 'f', 1) + QStringLiteral("°"); }

int quarter(int turn) { return ((turn % 4) + 4) % 4; }

} // namespace

QString MeasureController::levelCalKey(bool lying) const
{
    return QLatin1String(kLevelKey) + screenKey_ + (lying ? QStringLiteral("/lying") : QStringLiteral("/edge%1").arg(quarter(gravityTurn_)));
}

void MeasureController::loadLevelCalibration()
{
    levelCal_.loadedFor = screenKey_;
    levelCal_.loaded = true;
    QSettings settings;
    const QString lying = QLatin1String(kLevelKey) + screenKey_ + QStringLiteral("/lying");
    levelCal_.lyingKept = settings.contains(lying + QStringLiteral("X"));
    levelCal_.lyingOffset = levelCal_.lyingKept
                                ? Vec2{settings.value(lying + QStringLiteral("X")).toDouble(), settings.value(lying + QStringLiteral("Y")).toDouble()}
                                : Vec2{};
    for (int t = 0; t < 4; ++t) {
        const QString key = QLatin1String(kLevelKey) + screenKey_ + QStringLiteral("/edge%1").arg(t);
        levelCal_.edgeKept[std::size_t(t)] = settings.contains(key);
        levelCal_.edgeOffset[std::size_t(t)] = settings.value(key, 0.0).toDouble();
    }
}

void MeasureController::correctGravity(const GravityReading& reading)
{
    if (!levelCal_.loaded || levelCal_.loadedFor != screenKey_)
        loadLevelCalibration();
    rawGravity_ = {reading.x, reading.y, reading.z};
    gravityTurn_ = quarter(reading.turn);
    const double t = now();
    recentGravity_.emplace_back(t, rawGravity_);
    while (!recentGravity_.empty() && recentGravity_.front().first < t - 1.5)
        recentGravity_.pop_front();
    applyLevelCorrection();
}

void MeasureController::applyLevelCorrection()
{
    const measure::Tilt raw = measure::tiltFrom(rawGravity_);
    gravity_ = rawGravity_;
    if (raw.lying && levelCal_.lyingKept) {
        const measure::Gravity upright = measure::windowToUpright(rawGravity_, gravityTurn_);
        gravity_ = measure::uprightToWindow(measure::correctedLying(upright, levelCal_.lyingOffset), gravityTurn_);
    }
    tilt_ = measure::tiltFrom(gravity_);
    if (!raw.lying && levelCal_.edgeKept[std::size_t(gravityTurn_)])
        tilt_.edgeDeg = measure::relativeEdgeAngle(raw.edgeDeg, levelCal_.edgeOffset[std::size_t(gravityTurn_)]);
}

bool MeasureController::levelCalibrated() const
{
    return tilt_.lying ? levelCal_.lyingKept : levelCal_.edgeKept[std::size_t(quarter(gravityTurn_))];
}

double MeasureController::levelOffset() const
{
    if (!levelCalibrated())
        return 0;
    return tilt_.lying ? levelCal_.lyingOffset.length() : std::abs(levelCal_.edgeOffset[std::size_t(quarter(gravityTurn_))]);
}

void MeasureController::startLevelCalibration()
{
    levelReference_.reset();
    levelCal_.mode = QStringLiteral("calibrate");
    levelCal_.step = 1;
    levelCal_.result.clear();
    levelCal_.found = std::numeric_limits<double>::quiet_NaN();
    levelTrack_ = -1;
    level_.reset();
    OS_LOG(Info, App) << "measure: level calibration started";
    if (gravityAvailable_)
        feedLevel();
    emit levelChanged();
}

void MeasureController::startLevelCheck()
{
    startLevelCalibration();
    levelCal_.mode = QStringLiteral("check");
    OS_LOG(Info, App) << "measure: level check started";
    emit levelChanged();
}

void MeasureController::levelCalTurned()
{
    if (levelCal_.step != 2)
        return;
    levelCal_.result.clear();
    level_.clearHold();
    levelProgress_ = 0;
    levelArmed_ = true;
    emit levelChanged();
}

void MeasureController::endLevelCalibration()
{
    levelCal_.mode.clear();
    levelCal_.step = 0;
    levelCal_.result.clear();
    levelCal_.found = std::numeric_limits<double>::quiet_NaN();
    emit levelChanged();
}

void MeasureController::resetLevelCalibration()
{
    if (!levelCal_.loaded || levelCal_.loadedFor != screenKey_)
        loadLevelCalibration();
    QSettings settings;
    const bool lying = tilt_.lying;
    if (lying) {
        settings.remove(levelCalKey(true) + QStringLiteral("X"));
        settings.remove(levelCalKey(true) + QStringLiteral("Y"));
    } else {
        settings.remove(levelCalKey(false));
    }
    loadLevelCalibration();
    OS_LOG(Info, App) << "measure: level calibration reset (" << (lying ? "lying" : "standing") << ")";
    applyLevelCorrection();
    levelTrack_ = -1;
    if (gravityAvailable_)
        feedLevel();
    emit levelChanged();
}

// A hold during a calibration or a check: the first reading, or the second
// (turned) and the result.
void MeasureController::levelCalHold()
{
    // The raw readings of the steady second, averaged.
    const double t = now();
    Vec3 sum;
    int n = 0;
    for (const auto& [when, g] : recentGravity_)
        if (when >= t - 0.8) {
            sum = sum + Vec3{g.x, g.y, g.z};
            ++n;
        }
    const measure::Gravity raw = n > 0 ? measure::Gravity{sum.x / n, sum.y / n, sum.z / n} : rawGravity_;
    const bool lying = measure::tiltFrom(raw).lying;
    const int turn = gravityTurn_;
    const measure::Gravity reading = lying ? measure::windowToUpright(raw, turn) : raw;

    if (levelCal_.step == 1) {
        levelCal_.firstLying = lying;
        levelCal_.firstTurn = turn;
        levelCal_.first = reading;
        levelCal_.step = 2;
        levelCal_.result.clear();
        OS_LOG(Info, App) << "measure: level " << levelCal_.mode.toStdString() << ", first reading (" << (lying ? "lying" : "standing")
                          << "): " << raw.x << ", " << raw.y << ", " << raw.z;
        return;
    }
    if (lying != levelCal_.firstLying || (!lying && turn != levelCal_.firstTurn)) {
        levelCal_.result = levelCal_.firstLying ? QStringLiteral("Lay it down, as before.")
                           : lying              ? QStringLiteral("Stand it on its edge, as before.")
                                                : QStringLiteral("Stand it on the same edge as before.");
        level_.clearHold();
        return;
    }
    OS_LOG(Info, App) << "measure: level " << levelCal_.mode.toStdString() << ", turned reading: " << raw.x << ", " << raw.y << ", " << raw.z;
    const bool check = levelCal_.mode == QLatin1String("check");
    QSettings settings;
    if (lying) {
        if (check) {
            const measure::Gravity a = measure::correctedLying(levelCal_.first, levelCal_.lyingKept ? levelCal_.lyingOffset : Vec2{});
            const measure::Gravity b = measure::correctedLying(reading, levelCal_.lyingKept ? levelCal_.lyingOffset : Vec2{});
            const measure::FlipResult r = measure::lyingFlip(a, b);
            levelCal_.found = r.offsetSize();
            levelCal_.result = deg(measure::tiltFrom(a).slopeDeg) + QStringLiteral(" and ") + deg(measure::tiltFrom(b).slopeDeg)
                               + QStringLiteral(": off by ") + deg(levelCal_.found) + QStringLiteral(". ")
                               + (levelCal_.found <= kTrueWithinDeg ? QStringLiteral("The level reads true.") : QStringLiteral("Calibrate to correct it."));
        } else {
            const measure::FlipResult r = measure::lyingFlip(levelCal_.first, reading);
            if (r.offsetSize() > kMostOffsetDeg) {
                levelCal_.result = QStringLiteral("The two readings are too far apart: turn it in the same spot. Try again.");
                levelCal_.found = std::numeric_limits<double>::quiet_NaN();
            } else {
                levelCal_.lyingOffset = r.offsetDeg;
                levelCal_.lyingKept = true;
                settings.setValue(levelCalKey(true) + QStringLiteral("X"), r.offsetDeg.x);
                settings.setValue(levelCalKey(true) + QStringLiteral("Y"), r.offsetDeg.y);
                levelCal_.found = r.offsetSize();
                levelCal_.result = QStringLiteral("It read ") + deg(levelCal_.found) + QStringLiteral(" off lying down; that is taken off from now on. The surface sloped ")
                                   + deg(r.slopeSize()) + QStringLiteral(".");
            }
        }
    } else {
        const double e1 = measure::tiltFrom(levelCal_.first).edgeDeg, e2 = measure::tiltFrom(reading).edgeDeg;
        const std::size_t ti = std::size_t(turn);
        if (check) {
            const double off = levelCal_.edgeKept[ti] ? levelCal_.edgeOffset[ti] : 0.0;
            const double a = measure::relativeEdgeAngle(e1, off), b = measure::relativeEdgeAngle(e2, off);
            levelCal_.found = std::abs(measure::edgeFlip(a, b).offsetDeg.x);
            levelCal_.result = deg(a) + QStringLiteral(" and ") + deg(b) + QStringLiteral(": off by ") + deg(levelCal_.found) + QStringLiteral(". ")
                               + (levelCal_.found <= kTrueWithinDeg ? QStringLiteral("The level reads true.") : QStringLiteral("Calibrate to correct it."));
        } else {
            const measure::FlipResult r = measure::edgeFlip(e1, e2);
            if (std::abs(r.offsetDeg.x) > kMostOffsetDeg) {
                levelCal_.result = QStringLiteral("The two readings are too far apart: turn it end for end on the same spot. Try again.");
                levelCal_.found = std::numeric_limits<double>::quiet_NaN();
            } else {
                levelCal_.edgeOffset[ti] = r.offsetDeg.x;
                levelCal_.edgeKept[ti] = true;
                settings.setValue(levelCalKey(false), r.offsetDeg.x);
                levelCal_.found = std::abs(r.offsetDeg.x);
                levelCal_.result = QStringLiteral("It read ") + deg(levelCal_.found) + QStringLiteral(" off on this edge; that is taken off from now on. The surface sloped ")
                                   + deg(r.slopeDeg.x) + QStringLiteral(".");
            }
        }
    }
    OS_LOG(Info, App) << "measure: level " << levelCal_.mode.toStdString() << " done: " << levelCal_.result.toStdString();
    levelCal_.step = 3;
    // The reading shown starts again with the offset taken off.
    applyLevelCorrection();
    level_.reset();
    levelTrack_ = -1;
    if (devices_.haptic)
        devices_.haptic(3);
}

} // namespace os::ui
