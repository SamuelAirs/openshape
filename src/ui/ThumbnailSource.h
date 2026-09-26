// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>

namespace os::ui {

// Image sources for project previews on Home (ui/ThumbnailProvider serves
// them): "image://thumbnail/<stamp>/<path>". The path is written as
// base64url of its UTF-8 bytes, letters, digits, '-' and '_' only, so no
// decoding Qt applies to the URL on the way to the provider can change it
// (percent-encoding did: Qt hands the provider the id partly decoded, which
// broke every path with a character outside ASCII, like C:/Users/José).
// The stamp (the file's modification time) makes a re-saved project load
// its new preview instead of Qt's cached one.
QString thumbnailSource(const QString& path, qint64 stamp);

// The project path in the id Qt passes to the provider ("<stamp>/<path>",
// the source without "image://thumbnail/"); empty when it is malformed.
QString thumbnailPathFromId(const QString& id);

} // namespace os::ui
