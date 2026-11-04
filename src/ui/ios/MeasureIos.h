// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// What the iOS Measure files (src/ui/ios/*Ios.mm) share: the parts of
// MeasureDevices that live in their own files, and how to reach the Qt
// window's UIView and the view controller that presents over it. Included
// by Objective-C++ files only (iOS, ARC).

#include "ui/MeasureDevices.h"

#include <QtCore/QRectF>
#include <QtCore/QString>

#include <functional>
#include <vector>

#ifndef __OBJC__
#error "MeasureIos.h is for the iOS Objective-C++ files (src/ui/ios/*.mm)"
#endif

@class NSData;
@class NSString;
@class UIView;
@class UIViewController;

namespace os::ui::measureios {

// The UIView of `window` (Qt's QUIView: a QWindow's native id on iOS), or
// of the app's window when `window` is null; nil when there is none.
UIView* hostView(QWindow* window);

// The view controller to present over `view` from: the window's root view
// controller or what it already presents (the topmost one).
UIViewController* presenterFor(UIView* view);

// The message for a camera FoldForm may not use.
QString cameraDeniedMessage();

// Gravity in `view`'s frame (x right, y down, z out of the glass, by its
// scene's interface orientation) about 30 times a second, on the main
// thread, like MeasureDevices::watchGravity. Returns how to stop.
StopWatching watchGravityIn(UIView* view, std::function<void(const GravityReading&)> handler);

// PhotoPickerIos.mm.
void pickPhoto(QWindow* window, PhotoOrigin origin, const CameraOptions& camera, const QString& folder,
               const QRectF& anchor, std::function<void(const PhotoResult&)> finished);
void pickPhotos(QWindow* window, int maxCount, const QString& folder, const QRectF& anchor,
                std::function<void(const std::vector<PhotoResult>&)> finished);

// CameraScreenIos.mm: FoldForm's own camera screen (AVFoundation), full
// screen over `presenter`, with the level or Scan's angle and the overlay
// that `options` asks for. `done` runs once, on the main thread, after the
// screen started to go away: the photo as the camera encoded it (HEIC or
// JPEG, its EXIF orientation that of the preview as shown) and its 35 mm
// focal length (0: unknown); no photo and no error: cancelled. The camera
// permission is asked before (PhotoPickerIos.mm).
using CameraShotDone = std::function<void(NSData* photo, double focalLength35mm, NSString* error)>;
void presentCameraScreen(UIViewController* presenter, const CameraOptions& options, CameraShotDone done);

// ArMeasureIos.mm.
ArCapabilities arCapabilities();
void startAr(QWindow* window, ArMode mode, const QString& lengthUnit, std::function<void(const ArResult&)> finished);

} // namespace os::ui::measureios
