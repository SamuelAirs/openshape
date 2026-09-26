// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>
#include <QtQuick/QQuickImageProvider>

namespace os::ui {

// "image://thumbnail/<stamp>/<percent-encoded project path>": the preview a
// project file carries (io::readProjectThumbnail reads only that entry). The
// stamp (the file's modification time) makes a re-saved project load its new
// preview instead of Qt's cached one. A file without a usable preview gives
// a null image (QML then shows its placeholder). Loaded off the GUI thread.
class ThumbnailProvider final : public QQuickImageProvider {
public:
    ThumbnailProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // The image source for a project file.
    static QString sourceFor(const QString& path, qint64 stamp);
};

} // namespace os::ui
