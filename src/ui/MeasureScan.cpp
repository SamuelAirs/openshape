// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// MeasureController's Scan (docs/SCAN.md): the photos (camera, library,
// files), each one's paper corners (Vision, else measure::detectPaper, else
// placed for the owner to fix), the build on a worker thread with progress
// and Cancel (measure::runScan), the result, and the mesh as a body.

#include "core/Log.h"
#include "geometry/MeshSolid.h"
#include "measure/PaperDetect.h"
#include "measure/Scan.h"
#include "measure/ScanStereo.h"
#include "ui/AppController.h"
#include "ui/MeasureController.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QImageReader>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace os::ui {

namespace {

// The photos are kept at most this large (pixels on the long side): the
// engine works at 1600, and twelve photos stay small in memory.
constexpr int kScanPhotoSide = 2048;

QString q(const std::string& s) { return QString::fromStdString(s); }

QString fileOf(const QVariant& v)
{
    if (v.canConvert<QUrl>() && v.typeId() == QMetaType::QUrl) {
        const QUrl url = v.toUrl();
        return url.isLocalFile() ? url.toLocalFile() : url.toString();
    }
    const QString text = v.toString();
    const QUrl url(text);
    return url.isLocalFile() ? url.toLocalFile() : text;
}

measure::RgbImage rgbOf(const QImage& image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    measure::RgbImage out;
    out.width = rgb.width();
    out.height = rgb.height();
    out.rgb.resize(std::size_t(out.width) * out.height * 3);
    for (int y = 0; y < out.height; ++y)
        std::memcpy(out.rgb.data() + std::size_t(y) * out.width * 3, rgb.constScanLine(y), std::size_t(out.width) * 3);
    return out;
}

double signedVolume(const measure::ScanMesh& mesh)
{
    double v = 0;
    for (const auto& t : mesh.triangles) {
        const Vec3& a = mesh.vertices[std::size_t(t[0])];
        const Vec3& b = mesh.vertices[std::size_t(t[1])];
        const Vec3& c = mesh.vertices[std::size_t(t[2])];
        v += a.dot(b.cross(c));
    }
    return v / 6.0;
}

// The scan's main piece: loose bits (a speck of shadow, a stray corner of
// the table) are left out, so the body is one solid and its size is the
// object's. Returns how many pieces were dropped.
int keepLargestPiece(measure::ScanSurface& surface)
{
    const auto pieces = geom::meshPieces(geom::TriangleMesh{surface.mesh.vertices, surface.mesh.triangles});
    if (pieces.size() <= 1)
        return 0;
    std::size_t best = 0;
    double bestVolume = -1;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const double v = std::abs(signedVolume(measure::ScanMesh{pieces[i].vertices, pieces[i].triangles}));
        if (v > bestVolume) {
            bestVolume = v;
            best = i;
        }
    }
    surface.mesh = measure::ScanMesh{pieces[best].vertices, pieces[best].triangles};
    surface.volumeMm3 = bestVolume;
    surface.triangleCount = int(surface.mesh.triangles.size());
    surface.boundsMin = surface.boundsMax = surface.mesh.vertices.front();
    for (const Vec3& v : surface.mesh.vertices) {
        surface.boundsMin = {std::min(surface.boundsMin.x, v.x), std::min(surface.boundsMin.y, v.y), std::min(surface.boundsMin.z, v.z)};
        surface.boundsMax = {std::max(surface.boundsMax.x, v.x), std::max(surface.boundsMax.y, v.y), std::max(surface.boundsMax.z, v.z)};
    }
    return int(pieces.size()) - 1;
}

} // namespace

QVariantList MeasureController::scanPhotos() const
{
    QVariantList list;
    for (const ScanPhotoEntry& p : scanPhotos_) {
        QVariantList corners;
        for (const QPointF& c : p.corners)
            corners.append(c);
        list.append(QVariantMap{{QStringLiteral("url"), QUrl::fromLocalFile(p.file)},
                                {QStringLiteral("width"), p.width},
                                {QStringLiteral("height"), p.height},
                                {QStringLiteral("corners"), corners},
                                {QStringLiteral("found"), p.found},
                                {QStringLiteral("source"), p.source},
                                {QStringLiteral("mask"), !p.maskFile.isEmpty()},
                                {QStringLiteral("used"), p.used},
                                {QStringLiteral("problem"), p.problem}});
    }
    return list;
}

void MeasureController::setScanDetailFromStereo(bool on)
{
    if (on == scanDetailFromStereo_)
        return;
    scanDetailFromStereo_ = on;
    emit scanChanged();
}

void MeasureController::updateScanBusy()
{
    const bool busy = scanReading_ > 0;
    if (busy != scanBusy_) {
        scanBusy_ = busy;
        emit scanChanged();
    }
}

void MeasureController::runScanWork(std::function<void()> work, std::function<void()> done)
{
    const int generation = scanGeneration_;
    QThread* thread = QThread::create([work = std::move(work)] {
        try {
            work();
        } catch (const std::exception& e) {
            OS_LOG(Error, App) << "scan: work failed: " << e.what();
        }
    });
    thread->setStackSize(8u << 20);
    workers_.push_back(thread);
    connect(
        thread, &QThread::finished, this,
        [this, thread, generation, done = std::move(done)] {
            workers_.erase(std::remove(workers_.begin(), workers_.end(), thread), workers_.end());
            thread->wait();
            thread->deleteLater();
            if (generation == scanGeneration_)
                done();
        },
        Qt::QueuedConnection);
    thread->start();
}

void MeasureController::scanPickedPhoto(const PhotoResult& result)
{
    if (result.outcome == PhotoResult::Outcome::Picked)
        addScanFilesFrom({result.file}, true, {result.focalLength35mm});
    else if (result.outcome == PhotoResult::Outcome::Failed) {
        scanError_ = result.error.isEmpty() ? QStringLiteral("The photo couldn't be read.") : result.error;
        emit scanChanged();
    }
}

void MeasureController::scanTakePhoto(const QRectF& anchor)
{
    if (!devices_.pickPhoto || int(scanPhotos_.size()) >= kMaxScanPhotos)
        return;
    QDir().mkpath(photoFolder());
    // The camera screen shows the angle from straight down against Scan's band.
    CameraOptions camera;
    camera.purpose = CameraOptions::Purpose::Scan;
    QPointer<MeasureController> self(this);
    devices_.pickPhoto(window(), PhotoOrigin::Camera, camera, photoFolder(), anchor, [self](const PhotoResult& r) {
        if (self)
            self->scanPickedPhoto(r);
    });
}

void MeasureController::scanChoosePhotos(const QRectF& anchor)
{
    const int room = kMaxScanPhotos - int(scanPhotos_.size());
    if (room <= 0)
        return;
    QDir().mkpath(photoFolder());
    QPointer<MeasureController> self(this);
    if (devices_.pickPhotos) {
        devices_.pickPhotos(window(), room, photoFolder(), anchor, [self](const std::vector<PhotoResult>& results) {
            if (!self)
                return;
            QStringList files;
            QList<double> focals;
            for (const PhotoResult& r : results)
                if (r.outcome == PhotoResult::Outcome::Picked) {
                    files << r.file;
                    focals << r.focalLength35mm;
                }
            if (!files.isEmpty())
                self->addScanFilesFrom(files, true, focals);
        });
    } else if (devices_.pickPhoto) {
        devices_.pickPhoto(window(), PhotoOrigin::Library, CameraOptions{}, photoFolder(), anchor, [self](const PhotoResult& r) {
            if (self)
                self->scanPickedPhoto(r);
        });
    }
}

QVariantList MeasureController::takeNextScanFiles()
{
    QVariantList list;
    for (const QString& f : std::exchange(nextScanFiles_, {}))
        list.append(QUrl::fromLocalFile(f));
    return list;
}

void MeasureController::scanAddFiles(const QVariantList& files)
{
    QStringList list;
    for (const QVariant& v : files) {
        const QString f = fileOf(v);
        if (!f.isEmpty())
            list << f;
    }
    if (!list.isEmpty())
        addScanFilesFrom(list, false);
}

void MeasureController::addScanFilesFrom(const QStringList& filesIn, bool ownFiles, const QList<double>& focalsIn)
{
    const int room = kMaxScanPhotos - int(scanPhotos_.size());
    QStringList files = filesIn.mid(0, std::max(0, room));
    const QList<double> focals = focalsIn.mid(0, std::max(0, room));
    if (files.isEmpty()) {
        scanError_ = QStringLiteral("A scan takes up to %1 photos.").arg(kMaxScanPhotos);
        emit scanChanged();
        return;
    }
    if (ownFiles)
        for (const QString& f : files)
            if (!madeFiles_.contains(f))
                madeFiles_ << f;
    const QString folder = photoFolder();
    QDir().mkpath(folder);
    QStringList copies;
    for (const QString& f : files)
        copies << folder + QStringLiteral("/scan_%1_%2_%3.jpg")
                               .arg(QCoreApplication::applicationPid())
                               .arg(++fileCounter_)
                               .arg(QFileInfo(f).completeBaseName());
    ++scanReading_;
    scanError_.clear();
    updateScanBusy();
    emit scanChanged();

    auto results = std::make_shared<std::vector<ScanPhotoEntry>>();
    auto errors = std::make_shared<QStringList>();
    const auto detect = devices_.detectRectangle;
    const auto subject = devices_.subjectMask;
    runScanWork(
        [results, errors, files, copies, focals, detect, subject] {
            for (int i = 0; i < files.size(); ++i) {
                QImageReader reader(files[i]);
                reader.setAutoTransform(true);
                QImage image = reader.read();
                if (image.isNull()) {
                    *errors << QFileInfo(files[i]).fileName();
                    continue;
                }
                if (std::max(image.width(), image.height()) > kScanPhotoSide)
                    image = image.scaled(kScanPhotoSide, kScanPhotoSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                image = image.convertToFormat(QImage::Format_RGB32);
                if (!image.save(copies[i], "JPG", 93)) {
                    *errors << QFileInfo(files[i]).fileName();
                    continue;
                }
                ScanPhotoEntry e;
                e.file = copies[i];
                e.width = image.width();
                e.height = image.height();
                e.focal35mm = i < focals.size() && focals[i] > 0 ? focals[i] : 0.0;
                if (detect)
                    if (const auto c = detect(e.file)) {
                        for (int k = 0; k < 4; ++k)
                            e.corners[std::size_t(k)] = (*c)[std::size_t(k)];
                        e.found = true;
                        e.source = QStringLiteral("vision");
                    }
                if (!e.found)
                    if (const auto c = measure::detectPaper(rgbOf(image))) {
                        for (int k = 0; k < 4; ++k)
                            e.corners[std::size_t(k)] = QPointF((*c)[std::size_t(k)].x, (*c)[std::size_t(k)].y);
                        e.found = true;
                        e.source = QStringLiteral("detected");
                    }
                if (!e.found) {
                    const double w = e.width, h = e.height;
                    e.corners = {QPointF(0.2 * w, 0.3 * h), QPointF(0.8 * w, 0.3 * h), QPointF(0.85 * w, 0.8 * h), QPointF(0.15 * w, 0.8 * h)};
                }
                if (subject) {
                    const QString mask = e.file + QStringLiteral(".mask.png");
                    if (subject(e.file, mask))
                        e.maskFile = mask;
                }
                results->push_back(std::move(e));
            }
        },
        [this, results, errors] {
            --scanReading_;
            for (ScanPhotoEntry& e : *results) {
                madeFiles_ << e.file;
                if (!e.maskFile.isEmpty())
                    madeFiles_ << e.maskFile;
                scanPhotos_.push_back(std::move(e));
            }
            if (!errors->isEmpty())
                scanError_ = QStringLiteral("Couldn't read %1.").arg(errors->join(QStringLiteral(", ")));
            OS_LOG(Info, App) << "scan: " << results->size() << " photo(s) added, " << scanPhotos_.size() << " in all";
            updateScanBusy();
            emit scanChanged();
        });
}

void MeasureController::scanRemovePhoto(int index)
{
    if (index < 0 || index >= int(scanPhotos_.size()) || scanBuilding_)
        return;
    forgetFile(scanPhotos_[std::size_t(index)].file);
    forgetFile(scanPhotos_[std::size_t(index)].maskFile);
    scanPhotos_.erase(scanPhotos_.begin() + index);
    emit scanChanged();
}

void MeasureController::scanSetCorner(int photo, int corner, double x, double y)
{
    if (photo < 0 || photo >= int(scanPhotos_.size()) || corner < 0 || corner > 3 || !std::isfinite(x) || !std::isfinite(y))
        return;
    ScanPhotoEntry& e = scanPhotos_[std::size_t(photo)];
    e.corners[std::size_t(corner)] = QPointF(std::clamp(x, 0.0, double(e.width)), std::clamp(y, 0.0, double(e.height)));
    e.found = true;
    e.source = QStringLiteral("hand");
    emit scanChanged();
}

void MeasureController::setScanPaper(const QString& kind)
{
    const QString k = kind.trimmed().toLower() == QLatin1String("letter") ? QStringLiteral("letter") : QStringLiteral("a4");
    if (k == scanPaper_)
        return;
    scanPaper_ = k;
    emit scanChanged();
}

bool MeasureController::scanBuild()
{
    if (scanBuilding_ || scanBusy_ || scanPhotos_.size() < 3) {
        if (scanPhotos_.size() < 3) {
            scanError_ = QStringLiteral("Add at least three photos (eight to twelve from all around work best).");
            emit scanChanged();
        }
        return false;
    }
    scanBuilding_ = true;
    scanProgress_ = 0;
    scanStage_ = QStringLiteral("Reading the photos");
    scanError_.clear();
    scanResult_.clear();
    scanMesh_.reset();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    scanCancel_ = cancel;
    emit scanChanged();
    emit scanProgressChanged();

    std::vector<ScanPhotoEntry> photos = scanPhotos_;
    measure::ScanOptions options;
    options.paper = scanPaper_ == QLatin1String("letter") ? measure::letterReference() : measure::a4Reference();
    // The stages one by one (measure::runScan's), with multi-view stereo
    // (refineWithStereo) between the carving and the surface when asked.
    const bool stereo = scanDetailFromStereo_;
    struct Outcome {
        bool ok = false;
        bool cancelled = false;
        QString error;
        measure::ScanResult result;
        bool stereoUsed = false;
        double stereoMs = 0;
    };
    auto outcome = std::make_shared<Outcome>();
    auto progress = std::make_shared<std::atomic<int>>(0); // per mille
    QPointer<MeasureController> self(this);
    runScanWork(
        [outcome, photos, options, cancel, progress, self, stereo] {
            std::vector<QImage> images;
            std::vector<QImage> masks;
            images.reserve(photos.size());
            masks.reserve(photos.size());
            std::vector<measure::ScanPhoto> input;
            for (const ScanPhotoEntry& e : photos) {
                images.push_back(QImage(e.file).convertToFormat(QImage::Format_RGB888));
                masks.push_back(e.maskFile.isEmpty() ? QImage() : QImage(e.maskFile).convertToFormat(QImage::Format_Grayscale8));
                if (cancel->load()) {
                    outcome->cancelled = true;
                    return;
                }
            }
            for (std::size_t i = 0; i < photos.size(); ++i) {
                const QImage& img = images[i];
                if (img.isNull())
                    continue;
                measure::ScanPhoto p;
                p.pixels = img.constBits();
                p.width = img.width();
                p.height = img.height();
                p.stride = int(img.bytesPerLine());
                p.bytesPerPixel = 3;
                p.focal35mm = photos[i].focal35mm; // from EXIF when the picker found it
                for (int k = 0; k < 4; ++k)
                    p.paperCorners[std::size_t(k)] = {photos[i].corners[std::size_t(k)].x(), photos[i].corners[std::size_t(k)].y()};
                if (!masks[i].isNull()) {
                    p.subjectMask = masks[i].constBits();
                    p.maskWidth = masks[i].width();
                    p.maskHeight = masks[i].height();
                    p.maskStride = int(masks[i].bytesPerLine());
                }
                input.push_back(p);
            }
            // The stages' shares of the bar, and what each is called.
            const double calibrateEnd = stereo ? 0.3 : 0.5, carveEnd = stereo ? 0.45 : 0.8, stereoEnd = stereo ? 0.9 : 0.8;
            const auto stageReport = [progress, cancel, self](double from, double to, const QString& name) {
                return [progress, cancel, self, from, to, name](double f) {
                    const int perMille = int(std::clamp(from + (to - from) * f, 0.0, 1.0) * 1000);
                    if (perMille != progress->exchange(perMille))
                        QMetaObject::invokeMethod(
                            QCoreApplication::instance(),
                            [self, perMille, name] {
                                if (self && self->scanBuilding_) {
                                    self->scanProgress_ = perMille / 1000.0;
                                    self->scanStage_ = name;
                                    emit self->scanProgressChanged();
                                }
                            },
                            Qt::QueuedConnection);
                    return !cancel->load();
                };
            };
            using Clock = std::chrono::steady_clock;
            const auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            const auto fail = [&outcome, &cancel](const std::string& message) {
                outcome->cancelled = cancel->load();
                outcome->error = q(message);
            };
            const auto t0 = Clock::now();
            auto calibration = measure::calibrate(input, options, stageReport(0, calibrateEnd, QStringLiteral("Finding the paper and the cameras")));
            if (!calibration)
                return fail(calibration.userMessage());
            const auto t1 = Clock::now();
            auto hull = measure::carve(calibration->views, options, stageReport(calibrateEnd, carveEnd, QStringLiteral("Carving the shape")));
            if (!hull)
                return fail(hull.userMessage());
            const auto t2 = Clock::now();
            measure::ScanGrid field = hull.value();
            if (stereo) {
                measure::StereoStats stats;
                field = measure::refineWithStereo(calibration->views, hull.value(), measure::StereoOptions{},
                                                  stageReport(carveEnd, stereoEnd, QStringLiteral("Matching photos for detail")), &stats);
                if (cancel->load())
                    return fail("Cancelled.");
                outcome->stereoUsed = stats.viewsUsed >= 2;
            }
            const auto t3 = Clock::now();
            auto surface = measure::extractSurface(field, options, stageReport(stereoEnd, 1.0, QStringLiteral("Making the surface")));
            if (!surface)
                return fail(surface.userMessage());
            const auto t4 = Clock::now();
            measure::ScanResult& out = outcome->result;
            out.calibrateMs = ms(t0, t1);
            out.carveMs = ms(t1, t2);
            outcome->stereoMs = ms(t2, t3);
            out.surfaceMs = ms(t3, t4);
            out.photos = calibration->photos;
            out.focal35mm = calibration->focal35mm;
            out.focalEstimated = calibration->focalEstimated;
            const auto agreement = measure::silhouetteAgreement(calibration->views, field);
            for (std::size_t u = 0; u < calibration->views.size(); ++u)
                out.photos[std::size_t(calibration->views[u].photoIndex)].silhouetteAgreement = agreement[u];
            for (const auto* list : {&calibration.warnings(), &hull.warnings(), &surface.warnings()})
                for (const std::string& w : *list)
                    if (std::find(out.warnings.begin(), out.warnings.end(), w) == out.warnings.end())
                        out.warnings.push_back(w);
            out.surface = std::move(surface.value());
            if (const int dropped = keepLargestPiece(out.surface); dropped > 0)
                out.warnings.push_back(dropped == 1 ? "A small loose piece was left out (a shadow or a speck on the table)."
                                                    : std::to_string(dropped) + " small loose pieces were left out (shadows or specks on the table).");
            outcome->ok = true;
        },
        [this, outcome, cancel] {
            scanBuilding_ = false;
            if (scanCancel_ == cancel)
                scanCancel_.reset();
            if (outcome->cancelled) {
                OS_LOG(Info, App) << "scan: cancelled";
                emit scanChanged();
                emit scanFinished(QStringLiteral("cancelled"));
                return;
            }
            if (!outcome->ok) {
                scanError_ = outcome->error;
                OS_LOG(Warning, App) << "scan: " << outcome->error.toStdString();
                emit scanChanged();
                emit scanFinished(QStringLiteral("failed"));
                return;
            }
            const measure::ScanResult& r = outcome->result;
            const measure::ScanSurface& s = r.surface;
            scanMesh_ = std::make_shared<measure::ScanMesh>(s.mesh);
            QVariantList warnings;
            for (const std::string& w : r.warnings)
                warnings.append(q(w));
            int used = 0;
            for (std::size_t i = 0; i < scanPhotos_.size() && i < r.photos.size(); ++i) {
                scanPhotos_[i].used = r.photos[i].used;
                scanPhotos_[i].problem = q(r.photos[i].problem);
                used += r.photos[i].used ? 1 : 0;
            }
            scanResult_ = {{QStringLiteral("valid"), true},
                           {QStringLiteral("width"), s.boundsMax.x - s.boundsMin.x},
                           {QStringLiteral("depth"), s.boundsMax.y - s.boundsMin.y},
                           {QStringLiteral("height"), s.boundsMax.z - s.boundsMin.z},
                           {QStringLiteral("volume"), s.volumeMm3},
                           {QStringLiteral("triangles"), s.triangleCount},
                           {QStringLiteral("warnings"), warnings},
                           {QStringLiteral("used"), used},
                           {QStringLiteral("photos"), int(scanPhotos_.size())},
                           {QStringLiteral("focal35mm"), r.focal35mm},
                           {QStringLiteral("focalEstimated"), r.focalEstimated},
                           {QStringLiteral("stereo"), outcome->stereoUsed},
                           {QStringLiteral("ms"), r.calibrateMs + r.carveMs + outcome->stereoMs + r.surfaceMs}};
            scanProgress_ = 1;
            OS_LOG(Info, App) << "scan: " << s.triangleCount << " triangles, " << (s.boundsMax.x - s.boundsMin.x) << " x "
                              << (s.boundsMax.y - s.boundsMin.y) << " x " << (s.boundsMax.z - s.boundsMin.z) << " mm from " << used
                              << " photos in " << (r.calibrateMs + r.carveMs + outcome->stereoMs + r.surfaceMs) << " ms"
                              << (outcome->stereoUsed ? " (with stereo)" : "");
            emit scanProgressChanged();
            emit scanChanged();
            emit scanFinished(QStringLiteral("done"));
        });
    return true;
}

void MeasureController::scanCancel()
{
    if (scanCancel_)
        scanCancel_->store(true);
}

bool MeasureController::scanAddToDesign()
{
    if (!scanMesh_ || scanMesh_->triangles.empty())
        return false;
    return app_->addMeshBody(scanMesh_->vertices, scanMesh_->triangles, QString(), QStringLiteral("Scan"));
}

void MeasureController::scanDiscardResult()
{
    scanResult_.clear();
    scanMesh_.reset();
    for (ScanPhotoEntry& e : scanPhotos_) {
        e.used = false;
        e.problem.clear();
    }
    emit scanChanged();
}

void MeasureController::scanClear()
{
    scanCancel();
    ++scanGeneration_;
    scanReading_ = 0;
    scanBusy_ = false;
    scanBuilding_ = false;
    scanProgress_ = 0;
    scanStage_.clear();
    scanError_.clear();
    scanResult_.clear();
    scanMesh_.reset();
    for (const ScanPhotoEntry& e : scanPhotos_) {
        forgetFile(e.file);
        forgetFile(e.maskFile);
    }
    scanPhotos_.clear();
    emit scanChanged();
    emit scanProgressChanged();
}

bool MeasureController::waitForScan(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while ((scanBusy_ || scanBuilding_) && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return !scanBusy_ && !scanBuilding_;
}

} // namespace os::ui
