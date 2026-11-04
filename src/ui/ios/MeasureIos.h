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

// PhotoPickerIos.mm.
void pickPhoto(QWindow* window, PhotoOrigin origin, const QString& folder, const QRectF& anchor,
               std::function<void(const PhotoResult&)> finished);
void pickPhotos(QWindow* window, int maxCount, const QString& folder, const QRectF& anchor,
                std::function<void(const std::vector<PhotoResult>&)> finished);

// ArMeasureIos.mm.
ArCapabilities arCapabilities();
void startAr(QWindow* window, ArMode mode, const QString& lengthUnit, std::function<void(const ArResult&)> finished);

} // namespace os::ui::measureios
