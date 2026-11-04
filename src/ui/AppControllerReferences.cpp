// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference photos in the app (AppController.h, "Reference photos"): adding
// a picture from a file (the Measure workspace's straightened photo, File ->
// Add Reference Photo...) and Set scale by two points.

#include "ui/AppController.h"

#include "core/Log.h"
#include "core/Units.h"
#include "document/ReferenceImage.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QImage>
#include <QtGui/QImageReader>

#include <cmath>

namespace os::ui {

namespace {

QString qs(const std::string& s)
{
    return QString::fromStdString(s);
}

// A path, or a file URL's path (what a file dialog hands over).
QString pathOf(const QString& file)
{
    const QUrl url(file);
    if (url.isLocalFile())
        return url.toLocalFile();
    return file;
}

struct Picture {
    std::string bytes;
    std::string format; // "jpg" / "png"
    int width = 0, height = 0;   // as kept
    int sourceWidth = 0;         // as the file shows it (EXIF orientation applied), before any downscaling
    QString error;
};

// The picture as the project keeps it: the file's own bytes when it is a
// JPEG or PNG shown as it is stored and no larger than the document keeps;
// otherwise decoded (EXIF orientation applied), scaled down to
// kMaxReferenceImageSide and written as a JPEG (a PNG when it has
// transparency).
Picture readPicture(const QString& path)
{
    Picture out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("The picture could not be opened.");
        return out;
    }
    if (file.size() > qint64(256) * 1024 * 1024) {
        out.error = QStringLiteral("This picture is too large.");
        return out;
    }
    const QByteArray raw = file.readAll();
    QBuffer buffer;
    buffer.setData(raw);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    const QImageIOHandler::Transformations transform = reader.transformation();
    QImage image = reader.read();
    if (image.isNull()) {
        out.error = QStringLiteral("This picture could not be read. Use a JPEG or PNG photo.");
        OS_LOG(Warning, File) << "reference photo: " << path.toStdString() << ": " << reader.errorString().toStdString();
        return out;
    }
    out.sourceWidth = image.width();
    const int side = doc::kMaxReferenceImageSide;
    const bool keep = (format == "png" || format == "jpeg" || format == "jpg") && transform == QImageIOHandler::TransformationNone
                   && image.width() <= side && image.height() <= side
                   && std::uint64_t(raw.size()) <= doc::kMaxReferenceImageBytes;
    if (keep) {
        out.bytes.assign(raw.constData(), std::size_t(raw.size()));
        out.format = format == "png" ? "png" : "jpg";
        out.width = image.width();
        out.height = image.height();
        return out;
    }
    if (image.width() > side || image.height() > side)
        image = image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const bool alpha = image.hasAlphaChannel();
    QByteArray encoded;
    QBuffer target(&encoded);
    target.open(QIODevice::WriteOnly);
    if (!image.save(&target, alpha ? "PNG" : "JPG", alpha ? -1 : 92)) {
        out.error = QStringLiteral("This picture could not be read. Use a JPEG or PNG photo.");
        return out;
    }
    out.bytes.assign(encoded.constData(), std::size_t(encoded.size()));
    out.format = alpha ? "png" : "jpg";
    out.width = image.width();
    out.height = image.height();
    return out;
}

} // namespace

bool AppController::addReferenceImage(const QString& imageFile, double mmPerPixel, const QString& plane, double centerX, double centerY)
{
    doc::ReferenceImage reference;
    const QString p = plane.trimmed();
    if (p.isEmpty() || p.compare(QLatin1String("XY"), Qt::CaseInsensitive) == 0
        || p.compare(QLatin1String("ground"), Qt::CaseInsensitive) == 0 || p.compare(QLatin1String("top"), Qt::CaseInsensitive) == 0) {
        reference.plane = doc::ReferencePlane::XY;
    } else if (p.compare(QLatin1String("XZ"), Qt::CaseInsensitive) == 0 || p.compare(QLatin1String("front"), Qt::CaseInsensitive) == 0) {
        reference.plane = doc::ReferencePlane::XZ;
    } else if (p.compare(QLatin1String("YZ"), Qt::CaseInsensitive) == 0 || p.compare(QLatin1String("right"), Qt::CaseInsensitive) == 0) {
        reference.plane = doc::ReferencePlane::YZ;
    } else {
        const auto id = Uuid::parse(p.toStdString());
        const doc::Datum* datum = id ? document_->datum(*id) : nullptr;
        if (!datum || datum->kind() != doc::DatumKind::Plane) {
            emit message(QStringLiteral("There is no such plane for the photo."));
            return false;
        }
        reference.plane = doc::ReferencePlane::Datum;
        reference.datum = *id;
    }

    const QString path = pathOf(imageFile);
    const Picture picture = readPicture(path);
    if (!picture.error.isEmpty()) {
        emit message(picture.error);
        return false;
    }
    auto data = doc::makeReferenceImageData(picture.bytes, picture.width, picture.height, picture.format);
    if (!data) {
        emit message(qs(data.userMessage()));
        return false;
    }
    reference.image = data.value();
    reference.name = document_->nextReferenceName();
    if (std::isfinite(centerX) && std::isfinite(centerY))
        reference.center = {centerX, centerY};
    if (mmPerPixel > 0 && std::isfinite(mmPerPixel)) {
        reference.width = picture.sourceWidth * mmPerPixel;
        reference.unscaled = false;
    } else {
        reference.width = doc::kUnscaledReferenceWidth;
        reference.unscaled = true;
    }
    if (const Status valid = doc::checkReferenceValues(reference); !valid) {
        emit message(qs(valid.userMessage()));
        return false;
    }
    const Status status = interaction_->addReferenceImage(reference);
    if (!status)
        return false; // the controller said why
    OS_LOG(Info, File) << "added reference photo " << QFileInfo(path).fileName().toStdString() << " (" << picture.width << "x"
                       << picture.height << " " << picture.format << ", " << picture.bytes.size() << " bytes, "
                       << reference.width << " mm wide)";
    emit message(reference.unscaled ? QStringLiteral("Photo added. Set its scale in the Model panel: two points a known distance apart.")
                                    : QStringLiteral("Photo added at its real size."));
    return true;
}

// ---- Set scale by two points ---------------------------------------------------------------

void AppController::startReferenceScale(const QString& referenceId)
{
    const auto id = Uuid::parse(referenceId.toStdString());
    const doc::ReferenceImage* ref = id ? document_->reference(*id) : nullptr;
    if (!ref || !ref->hasImage())
        return;
    if (!ref->visible)
        (void)interaction_->setReferenceVisible(*id, true);
    ReferenceScaleState state;
    state.id = *id;
    referenceScale_ = state;
    // Looking at the photo, framed.
    interaction_->frameReference(*id, true);
    emit referenceScaleChanged();
}

void AppController::cancelReferenceScale()
{
    if (!referenceScale_)
        return;
    referenceScale_.reset();
    emit referenceScaleChanged();
}

void AppController::undoReferenceScalePoint()
{
    if (!referenceScale_ || referenceScale_->points.empty())
        return;
    referenceScale_->points.pop_back();
    emit referenceScaleChanged();
}

QString AppController::applyReferenceScale(const QString& distanceText)
{
    if (!referenceScale_ || referenceScale_->points.size() != 2)
        return QStringLiteral("Tap two points on the photo first.");
    const auto parsed = parseLength(distanceText.toStdString(), document_->displayUnit());
    if (!parsed.millimeters)
        return qs(parsed.error);
    const double real = *parsed.millimeters;
    if (!(real > 0))
        return QStringLiteral("Enter a distance above 0.");
    const Vec2 a = referenceScale_->points[0], b = referenceScale_->points[1];
    const double measured = std::hypot(b.x - a.x, b.y - a.y);
    if (!(measured > 1e-9))
        return QStringLiteral("The two points are the same: tap two different points.");
    const Status status = interaction_->scaleReference(referenceScale_->id, real / measured);
    if (!status)
        return qs(status.userMessage());
    referenceScale_.reset();
    emit referenceScaleChanged();
    emit message(QStringLiteral("Photo scaled: the two points are %1 apart.").arg(qs(formatLength(real, document_->displayUnit()))));
    return {};
}

bool AppController::referenceScalePointer(ScalePointer phase, QPointF position, bool touch)
{
    if (!referenceScale_)
        return false;
    auto& s = *referenceScale_;
    const Vec2 screen{position.x(), position.y()};
    const auto local = interaction_->referencePointAt(s.id, screen);
    switch (phase) {
    case ScalePointer::Hover:
        s.hover = s.points.size() < 2 ? local : std::nullopt;
        emit referenceScaleChanged();
        return true;
    case ScalePointer::Cancel:
        s.pending.reset();
        emit referenceScaleChanged();
        return true;
    case ScalePointer::Press:
        if (s.points.size() >= 2)
            return false; // both placed: the view is the view's again (the distance box waits)
        s.pending = local;
        s.touch = touch;
        s.finger = position;
        s.hover.reset();
        emit referenceScaleChanged();
        return true;
    case ScalePointer::Move:
        s.pending = local;
        s.finger = position;
        emit referenceScaleChanged();
        return true;
    case ScalePointer::Release: {
        s.pending.reset();
        const doc::ReferenceImage* ref = document_->reference(s.id);
        // On the photo (a little slack at its border).
        const bool onPhoto = ref && local && std::abs(local->x) <= ref->width * 0.51 && std::abs(local->y) <= ref->height() * 0.51;
        if (!onPhoto) {
            emit message(touch ? QStringLiteral("Tap a point on the photo.") : QStringLiteral("Click a point on the photo."));
        } else if (s.points.size() < 2) {
            s.points.push_back(*local);
        }
        emit referenceScaleChanged();
        return true;
    }
    }
    return false;
}

void AppController::refreshReferenceScale()
{
    if (!referenceScale_)
        return;
    const doc::ReferenceImage* ref = document_->reference(referenceScale_->id);
    if (!ref || !ref->hasImage()) {
        referenceScale_.reset();
    }
    emit referenceScaleChanged();
}

QVariantMap AppController::referenceScale() const
{
    QVariantMap map;
    map.insert(QStringLiteral("active"), referenceScale_.has_value());
    if (!referenceScale_)
        return map;
    const auto& s = *referenceScale_;
    const doc::ReferenceImage* ref = document_->reference(s.id);
    const bool touch = touchMode();
    map.insert(QStringLiteral("id"), qs(s.id.toString()));
    map.insert(QStringLiteral("name"), ref ? qs(ref->name) : QString());
    map.insert(QStringLiteral("step"), int(s.points.size()));
    const QString first = touch ? QStringLiteral("Tap one end of a distance you know on the photo")
                                : QStringLiteral("Click one end of a distance you know on the photo");
    const QString second = touch ? QStringLiteral("Now tap the other end") : QStringLiteral("Now click the other end");
    map.insert(QStringLiteral("hint"), s.points.empty() ? first : s.points.size() == 1 ? second
                                                                                     : QStringLiteral("Type the real distance between the two points"));
    auto screenOf = [&](const Vec2& local) -> QVariant {
        const auto p = interaction_->referenceScreenPoint(s.id, local);
        if (!p)
            return {};
        QVariantMap point;
        point.insert(QStringLiteral("x"), p->x);
        point.insert(QStringLiteral("y"), p->y);
        return point;
    };
    QVariantList points;
    for (const Vec2& p : s.points)
        points.append(screenOf(p));
    if (s.pending && s.points.size() < 2)
        points.append(screenOf(*s.pending));
    map.insert(QStringLiteral("points"), points);
    map.insert(QStringLiteral("cursor"), s.hover && !s.pending ? screenOf(*s.hover) : QVariant());
    // The distance measured on the photo as it is scaled now.
    std::optional<Vec2> a, b;
    if (!s.points.empty())
        a = s.points[0];
    if (s.points.size() >= 2)
        b = s.points[1];
    else if (s.pending && !s.points.empty())
        b = *s.pending;
    else if (s.hover && !s.points.empty())
        b = *s.hover;
    map.insert(QStringLiteral("measured"),
               a && b ? qs(formatLength(std::hypot(b->x - a->x, b->y - a->y), document_->displayUnit())) : QString());
    // The loupe: a finger placing a point hides it; magnified beside the finger.
    QVariantMap loupe;
    const bool loupeVisible = s.pending && s.touch;
    loupe.insert(QStringLiteral("visible"), loupeVisible);
    if (loupeVisible) {
        const auto subject = interaction_->referenceScreenPoint(s.id, *s.pending);
        constexpr double size = 112, gap = 36;
        double y = s.finger.y() - gap - size / 2;
        if (y - size / 2 < 8)
            y = s.finger.y() + gap + size / 2;
        loupe.insert(QStringLiteral("x"), s.finger.x());
        loupe.insert(QStringLiteral("y"), y);
        loupe.insert(QStringLiteral("sx"), subject ? subject->x : s.finger.x());
        loupe.insert(QStringLiteral("sy"), subject ? subject->y : s.finger.y());
        loupe.insert(QStringLiteral("size"), size);
        loupe.insert(QStringLiteral("zoom"), 2.5);
    }
    map.insert(QStringLiteral("loupe"), loupe);
    return map;
}

} // namespace os::ui
