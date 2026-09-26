// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>
#include <QtQuick/QQuickImageProvider>

namespace os::ui {

// The preview a project file carries (io::readProjectThumbnail reads only
// that entry), for image sources made by ui::thumbnailSource
// ("image://thumbnail/<stamp>/<encoded project path>", ui/ThumbnailSource.h).
// A file without a usable preview gives a null image (QML then shows its
// placeholder). Loaded off the GUI thread.
class ThumbnailProvider final : public QQuickImageProvider {
public:
    ThumbnailProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace os::ui
