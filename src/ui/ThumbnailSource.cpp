// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/ThumbnailSource.h"

#include <QtCore/QByteArray>

namespace os::ui {

namespace {

constexpr auto kEncoding = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;

} // namespace

QString thumbnailSource(const QString& path, qint64 stamp)
{
    return QStringLiteral("image://thumbnail/%1/%2")
        .arg(stamp)
        .arg(QString::fromLatin1(path.toUtf8().toBase64(kEncoding)));
}

QString thumbnailPathFromId(const QString& id)
{
    const qsizetype slash = id.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    const QByteArray encoded = id.mid(slash + 1).toLatin1();
    auto decoded = QByteArray::fromBase64Encoding(encoded, kEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty())
        return {};
    return QString::fromUtf8(decoded.decoded);
}

} // namespace os::ui
