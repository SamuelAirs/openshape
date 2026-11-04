// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/MeasureController.h"

#include "core/Log.h"
#include "core/Units.h"
#include "measure/Homography.h"
#include "measure/Outline.h"
#include "measure/ScreenScale.h"
#include "measure/StrokeFit.h"
#include "ui/AppController.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QScreen>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace os::ui {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
const char* const kScaleKey = "measure/scale/";
const char* const kCaliperKey = "measure/caliperArm";
// How often time passing re-feeds a still hinge (MeasureController::tickSensors).
constexpr int kSensorTickMs = 100;
const char* const kMeasurementsKey = "measure/measurements";
// The iPhone Duo's inner display, which the fold's halves are (in points).
constexpr double kDuoInnerLongPt = 951.0;
constexpr double kDuoInnerShortPt = 669.0;
constexpr double kCaliperBezelMm = 3.0;
constexpr int kMaxPhotoSide = 4096;

QString q(const std::string& s) { return QString::fromStdString(s); }

std::optional<measure::Quantity> quantityOf(const QString& name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("length"))
        return measure::Quantity::Length;
    if (lower == QLatin1String("angle"))
        return measure::Quantity::Angle;
    if (lower == QLatin1String("area"))
        return measure::Quantity::Area;
    return std::nullopt;
}

QString quantityKey(measure::Quantity quantity) { return q(measure::quantityName(quantity)).toLower(); }

std::optional<Vec2> pointOf(const QVariant& value)
{
    if (value.canConvert<QPointF>() && value.typeId() != QMetaType::QVariantMap) {
        const QPointF p = value.toPointF();
        return Vec2{p.x(), p.y()};
    }
    const QVariantMap map = value.toMap();
    if (map.contains(QStringLiteral("x")) && map.contains(QStringLiteral("y"))) {
        bool okX = false, okY = false;
        const double x = map.value(QStringLiteral("x")).toDouble(&okX);
        const double y = map.value(QStringLiteral("y")).toDouble(&okY);
        if (okX && okY && std::isfinite(x) && std::isfinite(y))
            return Vec2{x, y};
    }
    return std::nullopt;
}

std::vector<Vec2> pointsOf(const QVariantList& list)
{
    std::vector<Vec2> points;
    points.reserve(std::size_t(list.size()));
    for (const QVariant& v : list)
        if (const auto p = pointOf(v))
            points.push_back(*p);
    return points;
}

QVariantList doubles(const std::vector<double>& values)
{
    QVariantList list;
    for (double v : values)
        list.append(v);
    return list;
}

QString arModeName(ArMode mode)
{
    switch (mode) {
    case ArMode::Distance: return QStringLiteral("distance");
    case ArMode::Outline: return QStringLiteral("outline");
    case ArMode::Box: return QStringLiteral("box");
    }
    return QStringLiteral("distance");
}

std::optional<measure::Reference> referenceFor(const QString& kind, double a, double b)
{
    if (kind == QLatin1String("a4"))
        return measure::a4Reference();
    if (kind == QLatin1String("letter"))
        return measure::letterReference();
    if (kind == QLatin1String("rectangle"))
        return measure::rectangleReference(a, b);
    return measure::cardReference();
}

} // namespace

MeasureController::MeasureController(AppController* app) : QObject(app), app_(app)
{
    elapsed_.start();
    devices_ = platformMeasureDevices();
    ar_ = devices_.arCapabilities ? devices_.arCapabilities() : ArCapabilities{};
    lastUnit_ = app_->displayUnit();
    connect(app_, &AppController::stateChanged, this, &MeasureController::checkUnit);
    loadMeasurements();
    loadCaliper();
    sensorTick_ = new QTimer(this);
    sensorTick_->setInterval(kSensorTickMs);
    connect(sensorTick_, &QTimer::timeout, this, &MeasureController::tickSensors);
    // The window exists once QML has loaded; until MeasureScreen attaches
    // it, the scale is read from the primary screen.
    updateScale();
}

MeasureController::~MeasureController()
{
    stopSensors();
    for (QThread* thread : workers_)
        thread->wait();
    for (const QString& file : std::as_const(madeFiles_))
        QFile::remove(file);
}

// ---------------------------------------------------------------- window and scale

void MeasureController::attachWindow(QQuickWindow* window)
{
    if (window == window_)
        return;
    watchWindow(window);
    updateScale();
}

void MeasureController::watchWindow(QQuickWindow* window)
{
    for (const auto& c : std::as_const(windowConnections_))
        disconnect(c);
    windowConnections_.clear();
    window_ = window;
    if (!window)
        return;
    windowConnections_ << connect(window, &QWindow::screenChanged, this, &MeasureController::updateScale)
                       << connect(window, &QWindow::widthChanged, this, &MeasureController::updateScale)
                       << connect(window, &QWindow::heightChanged, this, &MeasureController::updateScale);
}

QQuickWindow* MeasureController::window() const
{
    if (window_)
        return window_;
    for (QWindow* w : QGuiApplication::topLevelWindows())
        if (auto* quick = qobject_cast<QQuickWindow*>(w))
            return quick;
    return nullptr;
}

void MeasureController::updateScale()
{
    QQuickWindow* w = window();
    QScreen* screen = w && w->screen() ? w->screen() : QGuiApplication::primaryScreen();
    if (!screen)
        return;
    if (screen != screen_) {
        for (const auto& c : std::as_const(screenConnections_))
            disconnect(c);
        screenConnections_.clear();
        screen_ = screen;
        screenConnections_ << connect(screen, &QScreen::geometryChanged, this, &MeasureController::updateScale)
                           << connect(screen, &QScreen::physicalSizeChanged, this, &MeasureController::updateScale);
    }
    measure::ScreenDescription d;
    const QSize size = screen->size();
    d.widthPt = size.width();
    d.heightPt = size.height();
    d.devicePixelRatio = screen->devicePixelRatio();
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // An iPhone (the Duo's two displays included) or an iPad, by the
    // screen's size in points (Split View does not change the screen).
    const bool duo = measure::isIphoneDuoInner(d.widthPt, d.heightPt) || measure::isIphoneDuoOuter(d.widthPt, d.heightPt);
    d.kind = duo || std::min(d.widthPt, d.heightPt) < 600 ? measure::DeviceKind::Phone : measure::DeviceKind::Tablet;
#else
    d.kind = measure::DeviceKind::Desktop;
    d.physicalWidthMm = screen->physicalSize().width();
    d.physicalHeightMm = screen->physicalSize().height();
#endif
    screenWidthPt_ = d.widthPt;
    screenHeightPt_ = d.heightPt;
    const QString key = q(measure::screenKey(d));
    const QString kind = d.kind == measure::DeviceKind::Phone    ? QStringLiteral("phone")
                         : d.kind == measure::DeviceKind::Tablet ? QStringLiteral("tablet")
                                                                 : QStringLiteral("desktop");
    // The fold's crease runs across the window only when the window is the
    // whole inner display (a Split View half lies on one side of it).
    bool crease = assumeFoldingDisplay_;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    crease = crease || (measure::isIphoneDuoInner(d.widthPt, d.heightPt) && w && measure::isIphoneDuoInner(w->width(), w->height()));
#endif
    const bool screenChanged = key != screenKey_ || kind != deviceKind_ || crease != foldCrease_;
    screenKey_ = key;
    deviceKind_ = kind;
    foldCrease_ = crease;
    QSettings settings;
    const double stored = settings.value(QLatin1String(kScaleKey) + key, 0.0).toDouble();
    if (measure::plausiblePointsPerMm(stored)) {
        setScale(stored, true, QStringLiteral("Checked with a card"));
    } else {
        const measure::ScaleEstimate estimate = measure::estimateScale(d);
        QString note = q(estimate.note);
        if (!note.isEmpty())
            note[0] = note[0].toUpper();
        setScale(estimate.pointsPerMm, false, note);
    }
    if (screenChanged)
        emit scaleChanged();
}

void MeasureController::setScale(double pointsPerMm, bool calibrated, const QString& note)
{
    if (pointsPerMm == pointsPerMm_ && calibrated == scaleCalibrated_ && note == scaleNote_)
        return;
    pointsPerMm_ = pointsPerMm;
    scaleCalibrated_ = calibrated;
    scaleNote_ = note;
    OS_LOG(Info, App) << "measure: the screen " << screenKey_.toStdString() << " is " << pointsPerMm_ << " points per mm ("
                      << note.toStdString() << ")";
    emit scaleChanged();
    app_->setScreenScale(pointsPerMm_, scaleCalibrated_);
}

QString MeasureController::unit() const
{
    const auto u = unitFromSymbol(app_->displayUnit().toStdString());
    return u == LengthUnit::Inch ? QStringLiteral("in") : QStringLiteral("mm");
}

const measure::CardSize& MeasureController::card() const { return measure::kPlayingCard; }

QString MeasureController::cardName() const { return QString::fromLatin1(card().name); }

QString MeasureController::cardSizeText() const
{
    // The short side first, as a card is held: "63.5 × 88.9 mm".
    const measure::CardSize& c = card();
    const auto tenths = [](double v) { return std::abs(v * 10 - std::round(v * 10)) < 1e-6; };
    const int decimals = tenths(c.shortMm) && tenths(c.longMm) ? 1 : 2;
    return QString::number(c.shortMm, 'f', decimals) + QStringLiteral(" × ") + QString::number(c.longMm, 'f', decimals)
           + QStringLiteral(" mm");
}

bool MeasureController::calibrate(double spanPt, double mm)
{
    const double ppm = measure::pointsPerMmFromSpan(spanPt, mm);
    if (!measure::plausiblePointsPerMm(ppm)) {
        OS_LOG(Warning, App) << "measure: a calibration of " << spanPt << " pt for " << mm << " mm was refused (implausible)";
        return false;
    }
    QSettings settings;
    settings.setValue(QLatin1String(kScaleKey) + screenKey_, ppm);
    setScale(ppm, true, QStringLiteral("Checked with a card"));
    if (devices_.haptic)
        devices_.haptic(2);
    return true;
}

void MeasureController::resetCalibration()
{
    QSettings settings;
    settings.remove(QLatin1String(kScaleKey) + screenKey_);
    updateScale();
}

void MeasureController::checkUnit()
{
    const QString symbol = app_->displayUnit();
    if (symbol == lastUnit_)
        return;
    lastUnit_ = symbol;
    emit unitChanged();
    emit measurementsChanged(); // their texts
}

// ---------------------------------------------------------------- sensors

double MeasureController::now() const { return clock_ ? clock_() : double(elapsed_.nsecsElapsed()) * 1e-9; }

void MeasureController::setClock(std::function<double()> clock) { clock_ = std::move(clock); }

void MeasureController::setAssumeFoldingDisplay(bool on)
{
    if (on == assumeFoldingDisplay_)
        return;
    assumeFoldingDisplay_ = on;
    updateScale();
}

void MeasureController::startSensors()
{
    if (sensorsRunning_)
        return;
    sensorsRunning_ = true;
    QQuickWindow* w = window();
    QPointer<MeasureController> self(this);
    if (devices_.watchHinge)
        stopHinge_ = devices_.watchHinge(w, [self](const HingeReading& r) {
            if (self)
                self->onHinge(r);
        });
    if (devices_.watchGravity)
        stopGravity_ = devices_.watchGravity(w, [self](const GravityReading& r) {
            if (self)
                self->onGravity(r);
        });
    sensorTick_->start();
    OS_LOG(Info, App) << "measure: sensors started (hinge " << (devices_.watchHinge ? "watched" : "none") << ", gravity "
                      << (devices_.watchGravity ? "watched" : "none") << ")";
    emit sensorsChanged();
}

void MeasureController::stopSensors()
{
    if (!sensorsRunning_)
        return;
    sensorsRunning_ = false;
    sensorTick_->stop();
    if (auto stop = std::exchange(stopHinge_, nullptr))
        stop();
    if (auto stop = std::exchange(stopGravity_, nullptr))
        stop();
    emit sensorsChanged();
}

void MeasureController::onHinge(const HingeReading& reading)
{
    if (!sensorsRunning_ || !reading.present)
        return;
    if (!hingeAvailable_) {
        hingeAvailable_ = true;
        emit sensorsChanged();
    }
    foldRadians_ = reading.angleRadians;
    feedFold(measure::hingeDegrees(reading.angleRadians));
    emit foldChanged();
}

bool MeasureController::feedFold(double degrees)
{
    lastHingeDegrees_ = degrees;
    lastHingeFeed_ = now();
    const measure::SteadyReading::State state = fold_.feed(degrees, lastHingeFeed_);
    const bool changed = state.value != foldAngle_ || state.steady != foldSteady_ || state.progress != foldProgress_
                         || state.armed != foldArmed_ || state.newHold;
    foldAngle_ = state.value;
    foldSteady_ = state.steady;
    foldProgress_ = state.progress;
    foldArmed_ = state.armed;
    if (state.newHold) {
        if (devices_.haptic)
            devices_.haptic(2);
        OS_LOG(Debug, App) << "measure: fold held at " << *state.held << " degrees";
        emit held(QStringLiteral("fold"));
    }
    return changed;
}

void MeasureController::tickSensors()
{
    // Only the hinge needs it: gravity streams at a steady rate.
    if (!sensorsRunning_ || !hingeAvailable_ || lastHingeFeed_ < 0 || now() - lastHingeFeed_ < 0.09)
        return;
    if (feedFold(lastHingeDegrees_))
        emit foldChanged();
}

double MeasureController::foldHeld() const { return fold_.held() ? *fold_.held() : kNaN; }

double MeasureController::caliperGapMm() const { return measure::caliperGapMm(foldAngle_ * kDegToRad, caliperArmMm_); }

double MeasureController::caliperHeldGapMm() const
{
    return fold_.held() ? measure::caliperGapMm(*fold_.held() * kDegToRad, caliperArmMm_) : kNaN;
}

double MeasureController::caliperMmPerDegree() const
{
    return measure::caliperMmPerDegree(foldAngle_ * kDegToRad, caliperArmMm_);
}

double MeasureController::defaultCaliperArm() const
{
    // Half the inner display's long side (the hinge runs down its middle)
    // plus the bezel, at the inner display's scale (its calibration if kept).
    const measure::ScreenDescription inner{kDuoInnerLongPt, kDuoInnerShortPt, 3.0, measure::DeviceKind::Phone};
    QSettings settings;
    double ppm = settings.value(QLatin1String(kScaleKey) + q(measure::screenKey(inner)), 0.0).toDouble();
    if (!measure::plausiblePointsPerMm(ppm))
        ppm = measure::estimateScale(inner).pointsPerMm;
    return kDuoInnerLongPt / ppm / 2.0 + kCaliperBezelMm;
}

void MeasureController::loadCaliper()
{
    QSettings settings;
    const double stored = settings.value(QLatin1String(kCaliperKey), 0.0).toDouble();
    caliperCalibrated_ = stored > 10 && stored < 400;
    caliperArmMm_ = caliperCalibrated_ ? stored : defaultCaliperArm();
}

bool MeasureController::calibrateCaliper(double knownGapMm)
{
    const double angle = fold_.held() ? *fold_.held() : foldAngle_;
    const auto arm = measure::armFromKnownGap(angle * kDegToRad, knownGapMm);
    if (!arm || *arm < 10 || *arm > 400)
        return false;
    caliperArmMm_ = *arm;
    caliperCalibrated_ = true;
    QSettings settings;
    settings.setValue(QLatin1String(kCaliperKey), caliperArmMm_);
    OS_LOG(Info, App) << "measure: caliper calibrated, arm " << caliperArmMm_ << " mm (a " << knownGapMm << " mm gap at "
                      << angle << " degrees)";
    if (devices_.haptic)
        devices_.haptic(2);
    emit foldChanged();
    return true;
}

void MeasureController::resetCaliper()
{
    QSettings settings;
    settings.remove(QLatin1String(kCaliperKey));
    loadCaliper();
    emit foldChanged();
}

void MeasureController::onGravity(const GravityReading& reading)
{
    if (!sensorsRunning_)
        return;
    if (!gravityAvailable_) {
        gravityAvailable_ = true;
        emit sensorsChanged();
    }
    gravity_ = {reading.x, reading.y, reading.z};
    tilt_ = measure::tiltFrom(gravity_);
    feedLevel();
    emit levelChanged();
}

void MeasureController::feedLevel()
{
    const int track = levelReference_ ? 2 : tilt_.lying ? 0 : 1;
    const double value = track == 2 ? relativeAngle() : track == 0 ? tilt_.slopeDeg : tilt_.edgeDeg;
    if (track != levelTrack_) {
        level_.reset();
        levelTrack_ = track;
    }
    const measure::SteadyReading::State state = level_.feed(value, now());
    levelValue_ = state.value;
    levelSteady_ = state.steady;
    levelProgress_ = state.progress;
    levelArmed_ = state.armed;
    if (state.newHold) {
        if (devices_.haptic)
            devices_.haptic(2);
        emit held(QStringLiteral("level"));
    }
}

double MeasureController::levelHeld() const { return level_.held() ? *level_.held() : kNaN; }

double MeasureController::relativeAngle() const
{
    return levelReference_ ? measure::angleBetween(*levelReference_, gravity_) : kNaN;
}

void MeasureController::keepLevelReference()
{
    levelReference_ = gravity_;
    levelTrack_ = -1;
    if (devices_.haptic)
        devices_.haptic(1);
    if (gravityAvailable_)
        feedLevel();
    emit levelChanged();
}

void MeasureController::clearLevelReference()
{
    levelReference_.reset();
    levelTrack_ = -1;
    if (gravityAvailable_)
        feedLevel();
    emit levelChanged();
}

void MeasureController::clearHold()
{
    fold_.clearHold();
    level_.clearHold();
    foldProgress_ = 0;
    foldArmed_ = true;
    levelProgress_ = 0;
    levelArmed_ = true;
    emit foldChanged();
    emit levelChanged();
}

// ---------------------------------------------------------------- photo

bool MeasureController::cameraAvailable() const { return devices_.pickPhoto && devices_.cameraAvailable && devices_.cameraAvailable(); }

QString MeasureController::photoFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/measure");
}

QVariantList MeasureController::corners() const
{
    QVariantList list;
    for (const QPointF& p : corners_)
        list.append(p);
    return list;
}

QString MeasureController::referenceName() const
{
    if (referenceKind_ == QLatin1String("a4"))
        return QStringLiteral("A4 paper");
    if (referenceKind_ == QLatin1String("letter"))
        return QStringLiteral("Letter paper");
    if (referenceKind_ == QLatin1String("rectangle"))
        return QStringLiteral("Rectangle");
    return cardName();
}

void MeasureController::setPhotoError(const QString& error)
{
    photoError_ = error;
    if (!error.isEmpty())
        OS_LOG(Warning, App) << "measure: " << error.toStdString();
    emit photoChanged();
}

void MeasureController::takePhoto(const QRectF& anchor)
{
    if (!devices_.pickPhoto)
        return setPhotoError(QStringLiteral("This device has no camera FoldForm can use."));
    QDir().mkpath(photoFolder());
    QPointer<MeasureController> self(this);
    devices_.pickPhoto(window(), PhotoOrigin::Camera, photoFolder(), anchor, [self](const PhotoResult& r) {
        if (!self)
            return;
        if (r.outcome == PhotoResult::Outcome::Picked)
            self->adoptPickedFile(r.file);
        else if (r.outcome == PhotoResult::Outcome::Failed)
            self->setPhotoError(r.error.isEmpty() ? QStringLiteral("The photo couldn't be taken.") : r.error);
    });
}

void MeasureController::choosePhoto(const QRectF& anchor)
{
    if (!devices_.pickPhoto)
        return setPhotoError(QStringLiteral("There is no photo library here: open an image file instead."));
    QDir().mkpath(photoFolder());
    QPointer<MeasureController> self(this);
    devices_.pickPhoto(window(), PhotoOrigin::Library, photoFolder(), anchor, [self](const PhotoResult& r) {
        if (!self)
            return;
        if (r.outcome == PhotoResult::Outcome::Picked)
            self->adoptPickedFile(r.file);
        else if (r.outcome == PhotoResult::Outcome::Failed)
            self->setPhotoError(r.error.isEmpty() ? QStringLiteral("The photo couldn't be read.") : r.error);
    });
}

void MeasureController::loadPhoto(const QUrl& url)
{
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (file.isEmpty())
        return;
    adoptPickedFile(file);
}

void MeasureController::forgetFile(const QString& file)
{
    // A view of the set still shows it.
    if (file.isEmpty() || !madeFiles_.contains(file) || fileInViews(file))
        return;
    madeFiles_.removeAll(file);
    QFile::remove(file);
}

void MeasureController::runPhotoWork(std::function<void()> work, std::function<void()> done)
{
    const int generation = photoGeneration_;
    QThread* thread = QThread::create([work = std::move(work)] {
        try {
            work();
        } catch (const std::exception& e) {
            OS_LOG(Error, App) << "measure: photo work failed: " << e.what();
        }
    });
    workers_.push_back(thread);
    connect(
        thread, &QThread::finished, this,
        [this, thread, generation, done = std::move(done)] {
            workers_.erase(std::remove(workers_.begin(), workers_.end(), thread), workers_.end());
            thread->wait();
            thread->deleteLater();
            if (generation == photoGeneration_)
                done();
        },
        Qt::QueuedConnection);
    thread->start();
}

void MeasureController::adoptPickedFile(const QString& file)
{
    ++photoGeneration_;
    if (photoViewIndex_ >= 0) {
        // A new photo is a new view (the one of the set stays as it was kept).
        photoViewIndex_ = -1;
        emit photoViewsChanged();
    }
    const QString folder = photoFolder();
    QDir().mkpath(folder);
    photoBusy_ = true;
    photoError_.clear();
    // A picked photo is written into the folder: it is ours to remove.
    if (QFileInfo(file).absolutePath() == QFileInfo(folder).absoluteFilePath() && !madeFiles_.contains(file))
        madeFiles_ << file;
    straightUrl_.clear();
    straightSize_ = {};
    straightReference_.clear();
    forgetFile(std::exchange(straightFile_, QString()));
    emit photoChanged();
    emit straightChanged();

    struct Result {
        QString file;
        QString error;
        QSize size;
        std::optional<RectangleCorners> corners;
        bool copied = false;
    };
    auto result = std::make_shared<Result>();
    const QString copy = folder + QStringLiteral("/photo_%1_%2.jpg").arg(QCoreApplication::applicationPid()).arg(++fileCounter_);
    const auto detect = devices_.detectRectangle;
    runPhotoWork(
        [result, file, folder, copy, detect] {
            QImageReader reader(file);
            reader.setAutoTransform(true);
            QImage image = reader.read();
            if (image.isNull()) {
                result->error = QStringLiteral("This image can't be read (%1).").arg(reader.errorString());
                return;
            }
            const bool turned = reader.transformation() != QImageIOHandler::TransformationNone;
            const bool large = std::max(image.width(), image.height()) > kMaxPhotoSide;
            if (large)
                image = image.scaled(kMaxPhotoSide, kMaxPhotoSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const bool inFolder = QFileInfo(file).absolutePath() == QFileInfo(folder).absoluteFilePath();
            result->file = file;
            if (turned || large || !inFolder) {
                // An upright copy of a size the warp handles, in the folder.
                if (!image.convertToFormat(QImage::Format_RGB32).save(copy, "JPG", 92)) {
                    result->error = QStringLiteral("The photo couldn't be copied for measuring.");
                    return;
                }
                result->file = copy;
                result->copied = true;
            }
            result->size = image.size();
            if (detect)
                result->corners = detect(result->file);
        },
        [this, result, file] {
            photoBusy_ = false;
            if (!result->error.isEmpty()) {
                setPhotoError(result->error);
                return;
            }
            if (result->copied)
                madeFiles_ << result->file;
            if (photoFile_ != result->file && photoFile_ != file)
                forgetFile(photoFile_);
            photoFile_ = result->file;
            photoUrl_ = QUrl::fromLocalFile(result->file);
            photoSize_ = QSizeF(result->size);
            const double w = result->size.width(), h = result->size.height();
            cornersDetected_ = false;
            if (result->corners) {
                bool inside = true;
                for (const QPointF& p : *result->corners)
                    inside = inside && p.x() >= -1 && p.y() >= -1 && p.x() <= w + 1 && p.y() <= h + 1;
                if (inside) {
                    for (int i = 0; i < 4; ++i)
                        corners_[std::size_t(i)] = (*result->corners)[std::size_t(i)];
                    cornersDetected_ = true;
                }
            }
            if (!cornersDetected_)
                corners_ = {QPointF(0.25 * w, 0.25 * h), QPointF(0.75 * w, 0.25 * h), QPointF(0.75 * w, 0.75 * h),
                            QPointF(0.25 * w, 0.75 * h)};
            OS_LOG(Info, App) << "measure: photo " << result->size.width() << "x" << result->size.height()
                              << (cornersDetected_ ? ", reference found" : ", corners by hand");
            emit photoChanged();
        });
}

void MeasureController::setCorner(int index, double x, double y)
{
    if (index < 0 || index > 3 || photoSize_.isEmpty() || !std::isfinite(x) || !std::isfinite(y))
        return;
    corners_[std::size_t(index)] = QPointF(std::clamp(x, 0.0, photoSize_.width()), std::clamp(y, 0.0, photoSize_.height()));
    if (!photoError_.isEmpty())
        photoError_.clear();
    emit photoChanged();
}

bool MeasureController::setReference(const QString& kind, double sideA, double sideB)
{
    const QString k = kind.trimmed().toLower();
    if (k != QLatin1String("card") && k != QLatin1String("a4") && k != QLatin1String("letter") && k != QLatin1String("rectangle"))
        return false;
    referenceKind_ = k;
    referenceSideA_ = sideA;
    referenceSideB_ = sideB;
    const bool usable = referenceFor(k, sideA, sideB).has_value();
    emit photoChanged();
    return usable;
}

bool MeasureController::straighten()
{
    if (photoFile_.isEmpty() || photoBusy_) {
        setPhotoError(photoBusy_ ? QString() : QStringLiteral("Take or choose a photo first."));
        return false;
    }
    const auto reference = referenceFor(referenceKind_, referenceSideA_, referenceSideB_);
    if (!reference) {
        setPhotoError(QStringLiteral("Type both sides of the rectangle (in millimeters)."));
        return false;
    }
    std::array<Vec2, 4> quad;
    for (std::size_t i = 0; i < 4; ++i)
        quad[i] = {corners_[i].x(), corners_[i].y()};
    const auto rectifier = measure::PlaneRectifier::create(quad, *reference);
    if (!rectifier) {
        setPhotoError(QStringLiteral("The corners don't outline a rectangle: drag each onto a corner of the %1.")
                          .arg(referenceName().toLower()));
        return false;
    }
    photoBusy_ = true;
    photoError_.clear();
    emit photoChanged();

    struct Result {
        QString error;
        measure::RectifiedView view;
        QVariantList reference;
    };
    auto result = std::make_shared<Result>();
    const QString out = photoFolder() + QStringLiteral("/straight_%1_%2.png").arg(QCoreApplication::applicationPid()).arg(++fileCounter_);
    const QString file = photoFile_;
    const measure::PlaneRectifier rect = *rectifier;
    runPhotoWork(
        [result, file, out, rect] {
            QImage photo(file);
            if (photo.isNull()) {
                result->error = QStringLiteral("The photo couldn't be read again.");
                return;
            }
            photo = photo.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const measure::RectifiedView view = measure::planRectifiedView(rect, photo.width(), photo.height());
            if (view.width <= 0 || view.height <= 0) {
                result->error = QStringLiteral("The photo can't be straightened on these corners.");
                return;
            }
            QImage straight(view.width, view.height, QImage::Format_ARGB32_Premultiplied);
            const measure::ImageBuffer buffer{photo.width(), photo.height(), int(photo.bytesPerLine()), photo.constBits()};
            measure::warpToView(buffer, rect, view, straight.bits(), int(straight.bytesPerLine()));
            if (!straight.save(out, "PNG", 80)) {
                result->error = QStringLiteral("The straightened photo couldn't be written.");
                return;
            }
            result->view = view;
            const double w = rect.referenceWidthMm(), h = rect.referenceHeightMm();
            for (const Vec2 corner : {Vec2{0, 0}, Vec2{w, 0}, Vec2{w, h}, Vec2{0, h}}) {
                const Vec2 p = view.toPixel(corner);
                result->reference.append(QPointF(p.x, p.y));
            }
        },
        [this, result, out] {
            photoBusy_ = false;
            if (!result->error.isEmpty()) {
                QFile::remove(out);
                setPhotoError(result->error);
                return;
            }
            madeFiles_ << out;
            forgetFile(std::exchange(straightFile_, out));
            straightUrl_ = QUrl::fromLocalFile(out);
            straightSize_ = QSizeF(result->view.width, result->view.height);
            straightMmPerPixel_ = result->view.mmPerPixel;
            straightOriginMm_ = QPointF(result->view.originMm.x, result->view.originMm.y);
            straightReference_ = result->reference;
            OS_LOG(Info, App) << "measure: straightened " << result->view.width << "x" << result->view.height << " at "
                              << result->view.mmPerPixel << " mm per pixel";
            emit photoChanged();
            emit straightChanged();
        });
    return true;
}

void MeasureController::unstraighten()
{
    straightUrl_.clear();
    straightSize_ = {};
    straightReference_.clear();
    forgetFile(std::exchange(straightFile_, QString()));
    emit straightChanged();
}

void MeasureController::clearPhoto()
{
    ++photoGeneration_;
    if (photoViewIndex_ >= 0) {
        photoViewIndex_ = -1;
        emit photoViewsChanged();
    }
    photoBusy_ = false;
    photoError_.clear();
    unstraighten();
    forgetFile(std::exchange(photoFile_, QString()));
    photoUrl_.clear();
    photoSize_ = {};
    cornersDetected_ = false;
    emit photoChanged();
}

QString MeasureController::keepStraightImage()
{
    if (straightFile_.isEmpty() || !QFileInfo::exists(straightFile_))
        return {};
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/measure");
    QDir().mkpath(folder);
    const QString copy = folder + QStringLiteral("/reference_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    if (!QFile::copy(straightFile_, copy)) {
        setPhotoError(QStringLiteral("The straightened photo couldn't be copied."));
        return {};
    }
    return copy;
}

// ---------------------------------------------------------------- photo views

bool MeasureController::fileInViews(const QString& file) const
{
    return std::any_of(photoViews_.begin(), photoViews_.end(),
                       [&file](const PhotoView& v) { return v.photoFile == file || v.straightFile == file; });
}

QVariantList MeasureController::photoViews() const
{
    QVariantList list;
    for (const PhotoView& v : photoViews_)
        list.append(QVariantMap{{QStringLiteral("name"), v.name},
                                {QStringLiteral("plane"), v.plane},
                                {QStringLiteral("straightUrl"), QUrl::fromLocalFile(v.straightFile)},
                                {QStringLiteral("width"), v.straightSize.width()},
                                {QStringLiteral("height"), v.straightSize.height()},
                                {QStringLiteral("mmPerPixel"), v.mmPerPixel},
                                {QStringLiteral("state"), v.state}});
    return list;
}

int MeasureController::savePhotoView(const QString& name, const QString& plane, const QVariantMap& state)
{
    if (straightFile_.isEmpty() || photoBusy_)
        return -1;
    const QString p = plane.trimmed().toUpper();
    PhotoView view;
    view.name = name.trimmed().isEmpty() ? QStringLiteral("View %1").arg(photoViews_.size() + 1) : name.trimmed().left(40);
    view.plane = p == QLatin1String("XZ") || p == QLatin1String("YZ") ? p : QStringLiteral("XY");
    view.photoFile = photoFile_;
    view.photoSize = photoSize_;
    view.corners = corners_;
    view.cornersDetected = cornersDetected_;
    view.referenceKind = referenceKind_;
    view.referenceSideA = referenceSideA_;
    view.referenceSideB = referenceSideB_;
    view.straightFile = straightFile_;
    view.straightSize = straightSize_;
    view.mmPerPixel = straightMmPerPixel_;
    view.originMm = straightOriginMm_;
    view.straightReference = straightReference_;
    view.state = state;
    if (photoViewIndex_ >= 0 && photoViewIndex_ < int(photoViews_.size())) {
        // The entry's files it no longer shows (straightened again) go.
        const PhotoView old = std::exchange(photoViews_[std::size_t(photoViewIndex_)], view);
        for (const QString& file : {old.photoFile, old.straightFile})
            if (file != photoFile_ && file != straightFile_)
                forgetFile(file);
    } else {
        photoViews_.push_back(view);
        photoViewIndex_ = int(photoViews_.size()) - 1;
    }
    OS_LOG(Info, App) << "measure: view " << view.name.toStdString() << " (" << view.plane.toStdString() << ") kept, "
                      << photoViews_.size() << " in the set";
    emit photoViewsChanged();
    return photoViewIndex_;
}

bool MeasureController::openPhotoView(int index)
{
    if (index < 0 || index >= int(photoViews_.size()) || photoBusy_)
        return false;
    ++photoGeneration_;
    const PhotoView view = photoViews_[std::size_t(index)];
    if (photoViewIndex_ < 0) {
        // The photo measured was not kept: its files go (unless a view shows them).
        forgetFile(std::exchange(straightFile_, QString()));
        forgetFile(std::exchange(photoFile_, QString()));
    }
    photoFile_ = view.photoFile;
    photoUrl_ = QUrl::fromLocalFile(view.photoFile);
    photoSize_ = view.photoSize;
    corners_ = view.corners;
    cornersDetected_ = view.cornersDetected;
    referenceKind_ = view.referenceKind;
    referenceSideA_ = view.referenceSideA;
    referenceSideB_ = view.referenceSideB;
    straightFile_ = view.straightFile;
    straightUrl_ = QUrl::fromLocalFile(view.straightFile);
    straightSize_ = view.straightSize;
    straightMmPerPixel_ = view.mmPerPixel;
    straightOriginMm_ = view.originMm;
    straightReference_ = view.straightReference;
    photoError_.clear();
    photoViewIndex_ = index;
    emit photoChanged();
    emit straightChanged();
    emit photoViewsChanged();
    return true;
}

void MeasureController::newPhotoView()
{
    clearPhoto();
}

bool MeasureController::removePhotoView(int index)
{
    if (index < 0 || index >= int(photoViews_.size()))
        return false;
    const PhotoView old = photoViews_[std::size_t(index)];
    photoViews_.erase(photoViews_.begin() + index);
    if (photoViewIndex_ == index)
        photoViewIndex_ = -1; // still measured, no longer in the set
    else if (photoViewIndex_ > index)
        --photoViewIndex_;
    for (const QString& file : {old.photoFile, old.straightFile})
        if (file != photoFile_ && file != straightFile_)
            forgetFile(file);
    emit photoViewsChanged();
    return true;
}

void MeasureController::clearPhotoViews()
{
    const std::vector<PhotoView> old = std::exchange(photoViews_, {});
    photoViewIndex_ = -1;
    for (const PhotoView& v : old)
        for (const QString& file : {v.photoFile, v.straightFile})
            if (file != photoFile_ && file != straightFile_)
                forgetFile(file);
    emit photoViewsChanged();
}

QPointF MeasureController::photoViewAnchor(const PhotoView& view) const
{
    if (const auto origin = pointOf(view.state.value(QStringLiteral("origin"))))
        return {origin->x, origin->y};
    // The middle of its closed outlines, else of the photo.
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    bool any = false;
    for (const QVariant& d : view.state.value(QStringLiteral("dims")).toList()) {
        const QVariantMap dim = d.toMap();
        if (dim.value(QStringLiteral("kind")).toString() != QLatin1String("outline") || !dim.value(QStringLiteral("closed")).toBool())
            continue;
        for (const Vec2 p : pointsOf(dim.value(QStringLiteral("points")).toList())) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
            any = true;
        }
    }
    if (any)
        return {(minX + maxX) / 2, (minY + maxY) / 2};
    return {view.straightSize.width() / 2, view.straightSize.height() / 2};
}

QPointF MeasureController::photoViewPlanePoint(int index, const QPointF& pixel) const
{
    if (index < 0 || index >= int(photoViews_.size()))
        return {};
    const PhotoView& view = photoViews_[std::size_t(index)];
    const QPointF anchor = photoViewAnchor(view);
    // The straightened photo's y runs down the picture, the plane's up.
    return {(pixel.x() - anchor.x()) * view.mmPerPixel, (anchor.y() - pixel.y()) * view.mmPerPixel};
}

int MeasureController::placePhotoViews(bool sketches)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/measure");
    QDir().mkpath(folder);
    int placed = 0;
    for (int i = 0; i < int(photoViews_.size()); ++i) {
        const PhotoView view = photoViews_[std::size_t(i)];
        if (view.straightFile.isEmpty() || !QFileInfo::exists(view.straightFile) || view.mmPerPixel <= 0)
            continue;
        const QString copy = folder + QStringLiteral("/reference_%1_%2.png").arg(QDateTime::currentMSecsSinceEpoch()).arg(i);
        if (!QFile::copy(view.straightFile, copy))
            continue;
        const QPointF center = photoViewPlanePoint(i, QPointF(view.straightSize.width() / 2, view.straightSize.height() / 2));
        if (!app_->addReferenceImage(copy, view.mmPerPixel, view.plane, center.x(), center.y()))
            continue;
        ++placed;
        madeLog_ << QStringLiteral("view %1 %2 %3 %4").arg(view.name, view.plane).arg(center.x(), 0, 'f', 2).arg(center.y(), 0, 'f', 2);
        if (!sketches)
            continue;
        for (const QVariant& d : view.state.value(QStringLiteral("dims")).toList()) {
            const QVariantMap dim = d.toMap();
            if (dim.value(QStringLiteral("kind")).toString() != QLatin1String("outline") || !dim.value(QStringLiteral("closed")).toBool())
                continue;
            QVariantList mm;
            for (const Vec2 p : pointsOf(dim.value(QStringLiteral("points")).toList()))
                mm.append(photoViewPlanePoint(i, QPointF(p.x, p.y)));
            if (mm.size() < 3)
                continue;
            lastOutlinePoints_ = mm;
            madeLog_ << QStringLiteral("outline %1 %2").arg(mm.size()).arg(view.plane);
            app_->createOutlineSketch(mm, true, view.plane);
        }
    }
    OS_LOG(Info, App) << "measure: " << placed << " of " << photoViews_.size() << " views placed in the design"
                      << (sketches ? ", outlines as sketches" : "");
    return placed;
}

double MeasureController::parseLengthMm(const QString& text) const
{
    const LengthUnit u = unitFromSymbol(app_->displayUnit().toStdString()).value_or(LengthUnit::Millimeter);
    const LengthParseResult result = parseLength(text.trimmed().toStdString(), u);
    return result.millimeters && *result.millimeters > 0 ? *result.millimeters : 0.0;
}

double MeasureController::distanceMm(const QPointF& a, const QPointF& b) const
{
    return std::hypot(b.x() - a.x(), b.y() - a.y()) * straightMmPerPixel_;
}

QVariantList MeasureController::straightToMm(const QVariantList& points) const
{
    QVariantList list;
    for (const Vec2 p : pointsOf(points))
        list.append(QPointF(straightOriginMm_.x() + p.x * straightMmPerPixel_, straightOriginMm_.y() + p.y * straightMmPerPixel_));
    return list;
}

QVariantMap MeasureController::outlineReport(const QVariantList& points, bool closed) const
{
    std::vector<Vec2> mm;
    for (const Vec2 p : pointsOf(points))
        mm.push_back({straightOriginMm_.x() + p.x * straightMmPerPixel_, straightOriginMm_.y() + p.y * straightMmPerPixel_});
    const measure::OutlineReport report = measure::describeOutline(mm, closed);
    return {{QStringLiteral("sides"), doubles(report.sideLengths)},
            {QStringLiteral("corners"), doubles(report.cornerAngles)},
            {QStringLiteral("perimeter"), report.perimeter},
            {QStringLiteral("area"), report.area},
            {QStringLiteral("width"), report.boundsMax.x - report.boundsMin.x},
            {QStringLiteral("height"), report.boundsMax.y - report.boundsMin.y}};
}

QVariantMap MeasureController::circleFrom3(const QPointF& a, const QPointF& b, const QPointF& c) const
{
    const auto circle = measure::circleThrough({a.x(), a.y()}, {b.x(), b.y()}, {c.x(), c.y()});
    if (!circle)
        return {{QStringLiteral("valid"), false}};
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("center"), QPointF(circle->center.x, circle->center.y)},
            {QStringLiteral("radius"), circle->radius},
            {QStringLiteral("diameter"), 2.0 * circle->radius * straightMmPerPixel_}};
}

bool MeasureController::waitForPhoto(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while ((photoBusy_ || !workers_.empty()) && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return !photoBusy_;
}

// ---------------------------------------------------------------- AR

bool MeasureController::startAr(const QString& mode)
{
    if (!arAvailable() || arBusy_)
        return false;
    const QString m = mode.trimmed().toLower();
    const ArMode arMode = m == QLatin1String("box") ? ArMode::Box : m == QLatin1String("outline") ? ArMode::Outline : ArMode::Distance;
    arBusy_ = true;
    emit arChanged();
    QPointer<MeasureController> self(this);
    devices_.startAr(window(), arMode, unit(), [self](const ArResult& r) {
        if (!self)
            return;
        QVariantMap map;
        map[QStringLiteral("outcome")] = r.outcome == ArResult::Outcome::Done        ? QStringLiteral("done")
                                         : r.outcome == ArResult::Outcome::Cancelled ? QStringLiteral("cancelled")
                                                                                     : QStringLiteral("failed");
        map[QStringLiteral("mode")] = arModeName(r.mode);
        map[QStringLiteral("error")] = r.error;
        map[QStringLiteral("distances")] = doubles(r.distancesMm);
        double total = 0;
        for (double d : r.distancesMm)
            total += d;
        map[QStringLiteral("total")] = total;
        QVariantList outline;
        std::vector<Vec2> points;
        for (const QPointF& p : r.outlineMm) {
            outline.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
            points.push_back({p.x(), p.y()});
        }
        map[QStringLiteral("outline")] = outline;
        const measure::OutlineReport report = measure::describeOutline(points, true);
        map[QStringLiteral("sides")] = doubles(report.sideLengths);
        map[QStringLiteral("perimeter")] = report.perimeter;
        map[QStringLiteral("area")] = report.area;
        map[QStringLiteral("boundsWidth")] = report.boundsMax.x - report.boundsMin.x;
        map[QStringLiteral("boundsHeight")] = report.boundsMax.y - report.boundsMin.y;
        map[QStringLiteral("width")] = r.widthMm;
        map[QStringLiteral("depth")] = r.depthMm;
        map[QStringLiteral("height")] = r.heightMm;
        self->arBusy_ = false;
        // A cancelled view keeps the earlier result.
        if (r.outcome != ArResult::Outcome::Cancelled)
            self->arResult_ = map;
        OS_LOG(Info, App) << "measure: AR " << arModeName(r.mode).toStdString() << " "
                          << map.value(QStringLiteral("outcome")).toString().toStdString();
        emit self->arChanged();
        emit self->arFinished();
    });
    return true;
}

void MeasureController::clearArResult()
{
    arResult_.clear();
    emit arChanged();
}

// ---------------------------------------------------------------- measurements

void MeasureController::loadMeasurements()
{
    QSettings settings;
    const QString json = settings.value(QLatin1String(kMeasurementsKey)).toString();
    if (!json.isEmpty() && !log_.fromJson(json.toStdString()))
        OS_LOG(Warning, App) << "measure: the kept measurements could not be read";
}

void MeasureController::saveMeasurements() const
{
    QSettings settings;
    settings.setValue(QLatin1String(kMeasurementsKey), q(log_.toJson()));
}

QVariantList MeasureController::measurements() const
{
    QVariantList list;
    for (const measure::Measurement& m : log_.items())
        list.append(measurement(q(m.id)));
    return list;
}

QVariantMap MeasureController::measurement(const QString& id) const
{
    const measure::Measurement* m = log_.find(id.toStdString());
    if (!m)
        return {};
    const QString quantity = quantityKey(m->quantity);
    return {{QStringLiteral("id"), q(m->id)},
            {QStringLiteral("name"), q(m->name)},
            {QStringLiteral("quantity"), quantity},
            {QStringLiteral("value"), m->value},
            {QStringLiteral("text"), formatValue(quantity, m->value)},
            {QStringLiteral("valueText"), valueText(quantity, m->value)},
            {QStringLiteral("source"), q(m->source)}};
}

QString MeasureController::addMeasurement(const QString& quantity, double value, const QString& source, const QString& name)
{
    const auto qty = quantityOf(quantity);
    if (!qty || !std::isfinite(value))
        return {};
    const std::string id = log_.add(*qty, value, source.toStdString(), QDateTime::currentMSecsSinceEpoch(), name.trimmed().toStdString());
    saveMeasurements();
    OS_LOG(Info, App) << "measure: kept " << formatValue(quantity, value).toStdString() << " from " << source.toStdString();
    emit measurementsChanged();
    return q(id);
}

bool MeasureController::renameMeasurement(const QString& id, const QString& name)
{
    if (name.trimmed().isEmpty() || !log_.rename(id.toStdString(), name.trimmed().toStdString()))
        return false;
    saveMeasurements();
    emit measurementsChanged();
    return true;
}

bool MeasureController::removeMeasurement(const QString& id)
{
    if (!log_.remove(id.toStdString()))
        return false;
    saveMeasurements();
    emit measurementsChanged();
    return true;
}

QString MeasureController::formatValue(const QString& quantity, double value) const
{
    const auto qty = quantityOf(quantity);
    if (!qty || !std::isfinite(value))
        return {};
    const LengthUnit u = unitFromSymbol(app_->displayUnit().toStdString()).value_or(LengthUnit::Millimeter);
    return q(measure::formatMeasurement(*qty, value, u));
}

QString MeasureController::valueText(const QString& quantity, double value) const
{
    const auto qty = quantityOf(quantity);
    if (!qty || !std::isfinite(value))
        return {};
    const LengthUnit u = unitFromSymbol(app_->displayUnit().toStdString()).value_or(LengthUnit::Millimeter);
    return q(measure::valueText(*qty, value, u));
}

QString MeasureController::formatNumber(const QString& quantity, double value) const
{
    // formatValue's number, without its unit ("42.3", "3000"); an angle
    // keeps its degree sign ("37.5\u00B0"), which belongs to the number.
    const QString text = formatValue(quantity, value);
    const qsizetype space = text.lastIndexOf(QLatin1Char(' '));
    return space > 0 ? text.left(space) : text;
}

QString MeasureController::unitText(const QString& quantity, double value) const
{
    // formatValue's unit as written for this value ("mm", "cm\u00B2"; "" for an angle).
    const QString text = formatValue(quantity, value);
    const qsizetype space = text.lastIndexOf(QLatin1Char(' '));
    return space > 0 ? text.mid(space + 1) : QString();
}

QString MeasureController::unitSymbol(const QString& quantity) const
{
    const auto qty = quantityOf(quantity);
    if (!qty)
        return {};
    if (*qty == measure::Quantity::Angle)
        return QStringLiteral("°");
    const LengthUnit u = unitFromSymbol(app_->displayUnit().toStdString()).value_or(LengthUnit::Millimeter);
    if (*qty == measure::Quantity::Area) {
        // As formatMeasurement writes it (large areas in cm²; the caller
        // takes the symbol off the formatted text, so the value decides).
        return u == LengthUnit::Inch ? QStringLiteral("in²") : QStringLiteral("mm²");
    }
    return q(std::string(os::unitSymbol(u)));
}

// ---------------------------------------------------------------- making geometry

bool MeasureController::makeOutlineSketch(const QVariantList& pointsMm, bool flipY)
{
    const std::vector<Vec2> points = pointsOf(pointsMm);
    if (points.size() < 3)
        return false;
    QVariantList list;
    for (const Vec2 p : measure::centeredOnOrigin(points, flipY))
        list.append(QPointF(p.x, p.y));
    lastOutlinePoints_ = list;
    madeLog_ << QStringLiteral("outline %1").arg(list.size());
    OS_LOG(Info, App) << "measure: an outline sketch of " << list.size() << " points";
    return app_->createOutlineSketch(list, true);
}

bool MeasureController::makeCircleSketch(double diameterMm)
{
    if (!(diameterMm > 0) || !std::isfinite(diameterMm))
        return false;
    madeLog_ << QStringLiteral("circle %1").arg(diameterMm, 0, 'f', 2);
    OS_LOG(Info, App) << "measure: a circle sketch, diameter " << diameterMm << " mm";
    return app_->createCircleSketch(0, 0, diameterMm);
}

bool MeasureController::makeBox(double widthMm, double depthMm, double heightMm)
{
    if (!(widthMm > 0) || !(depthMm > 0) || !(heightMm > 0))
        return false;
    madeLog_ << QStringLiteral("box %1 %2 %3").arg(widthMm, 0, 'f', 1).arg(depthMm, 0, 'f', 1).arg(heightMm, 0, 'f', 1);
    OS_LOG(Info, App) << "measure: a box " << widthMm << " x " << depthMm << " x " << heightMm << " mm";
    return app_->createSizedBox(widthMm, depthMm, heightMm);
}

// ---------------------------------------------------------------- devices

void MeasureController::setDevices(MeasureDevices devices)
{
    const bool running = sensorsRunning_;
    stopSensors();
    devices_ = std::move(devices);
    ar_ = devices_.arCapabilities ? devices_.arCapabilities() : ArCapabilities{};
    hingeAvailable_ = false;
    gravityAvailable_ = false;
    fold_.reset();
    level_.reset();
    levelReference_.reset();
    levelTrack_ = -1;
    foldAngle_ = 0;
    foldSteady_ = false;
    foldProgress_ = 0;
    foldArmed_ = true;
    lastHingeFeed_ = -1;
    levelValue_ = 0;
    levelSteady_ = false;
    levelProgress_ = 0;
    levelArmed_ = true;
    tilt_ = {};
    if (running)
        startSensors();
    emit sensorsChanged();
    emit foldChanged();
    emit levelChanged();
    emit arChanged();
}

} // namespace os::ui
