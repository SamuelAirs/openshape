// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/ThumbnailProvider.h"

#include "io/ProjectFile.h"
#include "ui/ThumbnailSource.h"

#include <QtGui/QImage>

namespace os::ui {

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

QImage ThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString path = thumbnailPathFromId(id);
    QImage image;
    if (!path.isEmpty())
        if (auto png = io::readProjectThumbnail(std::filesystem::path(path.toStdWString())))
            image = QImage::fromData(png.value().data(), int(png.value().size()), "PNG");
    if (!image.isNull() && requestedSize.isValid())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}

} // namespace os::ui
