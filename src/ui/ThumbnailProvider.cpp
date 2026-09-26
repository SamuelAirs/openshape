// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/ThumbnailProvider.h"

#include "io/ProjectFile.h"

#include <QtCore/QUrl>
#include <QtGui/QImage>

namespace os::ui {

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

QString ThumbnailProvider::sourceFor(const QString& path, qint64 stamp)
{
    return QStringLiteral("image://thumbnail/%1/%2")
        .arg(stamp)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(path)));
}

QImage ThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const qsizetype slash = id.indexOf(QLatin1Char('/'));
    const QString path = QUrl::fromPercentEncoding(id.mid(slash + 1).toLatin1());
    QImage image;
    if (auto png = io::readProjectThumbnail(std::filesystem::path(path.toStdWString())))
        image = QImage::fromData(png.value().data(), int(png.value().size()), "PNG");
    if (!image.isNull() && requestedSize.isValid())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}

} // namespace os::ui
