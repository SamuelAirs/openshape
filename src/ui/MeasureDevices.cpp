// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/MeasureDevices.h"

#include <QtCore/QtGlobal>

namespace os::ui {

#if !defined(Q_OS_IOS)
// A desktop has no hinge, motion sensors, camera picker or ARKit: the
// Measure tools that need them say so, and a photo comes from a file
// (MeasureScreen's file dialog). iOS: src/ui/ios/MeasureDevicesIos.mm.
MeasureDevices platformMeasureDevices()
{
    return {};
}
#endif

} // namespace os::ui
