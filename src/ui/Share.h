// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QRectF>
#include <QtCore/QString>

#include <functional>

namespace os::ui {

// The system share sheet (iPhone / iPad: AirDrop, Save to Files, Mail and
// the apps that take the file, such as a slicer for STL and 3MF). The app
// calls a handler (AppController::setShareHandler): the platform's
// (platformShareHandler), or a stub in the acceptance run, which cannot
// click a native sheet.

struct ShareRequest {
    QString file;  // an existing file, absolute path
    QRectF anchor; // the control that asked, in window coordinates (the iPad's popover points at it); may be empty
};

enum class ShareOutcome { Completed, Cancelled, Failed };

// Called once, on the GUI thread, when the sheet has closed. `detail`: the
// activity chosen (Completed) or the error (Failed).
using ShareFinished = std::function<void(ShareOutcome outcome, const QString& detail)>;
using ShareHandler = std::function<void(const ShareRequest& request, ShareFinished finished)>;

// iOS: UIActivityViewController over the app's window, on the main thread
// (src/ui/ios/ShareSheet.mm). Elsewhere empty: no share sheet.
ShareHandler platformShareHandler();

} // namespace os::ui
