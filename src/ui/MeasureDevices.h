// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QString>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QWindow;

namespace os::ui {

// The device's own measuring hardware, for the Measure tools
// (docs/MEASURE.md): the iPhone Duo's hinge, the motion sensors' gravity,
// the camera and the photo library, Vision's rectangle finder, and ARKit.
// Each is a function that is empty where the platform has none
// (platformMeasureDevices: iOS fills them in src/ui/ios/, the desktop has
// none); the acceptance run puts in stand-ins it controls
// (MeasureController::setDevices). Every callback runs on the GUI thread.

// UIHinge's status (UIKit's UIHingeStatus values).
enum class HingeStatus { Unknown = 0, Closed = 1, PartiallyOpen = 2, FullyOpen = 3 };

struct HingeReading {
    bool present = false;     // false: the window left a hierarchy that reports a hinge (or the phone has none)
    double angleRadians = 0;  // UIHinge.angle: 0 closed, pi flat
    HingeStatus status = HingeStatus::Unknown;
};

// Gravity in the window's frame: x to the right of the screen as shown, y
// down it, z out of the glass toward the viewer, in g (lying face up on a
// level table: (0, 0, -1)).
struct GravityReading {
    double x = 0;
    double y = 0;
    double z = -1;
};

enum class PhotoOrigin { Camera, Library };

// What Take photo's camera screen shows (pickPhoto from the camera; the
// photo library ignores it).
struct CameraOptions {
    enum class Purpose {
        Photo, // a level for a photo from straight above the card
        Scan   // the camera's angle from straight down against Scan's band (about 45 degrees)
    };
    Purpose purpose = Purpose::Photo;
    // The session's previous photo, offered as a faint overlay on the
    // preview (keep the card still, move the object, match the view); "": none.
    QString overlayFile;
};

struct PhotoResult {
    enum class Outcome { Picked, Cancelled, Failed };
    Outcome outcome = Outcome::Failed;
    QString file;  // Picked: a JPEG, upright (EXIF orientation applied), in the folder asked for
    QString error; // Failed: why, in words
    // The camera's focal length as a 35 mm film equivalent, from the original photo's
    // EXIF (kCGImagePropertyExifFocalLenIn35mmFilm), before it was re-encoded; 0 if unknown.
    double focalLength35mm = 0;
};

// Corners of a rectangle found in a photo, in image pixels, in order around it.
using RectangleCorners = std::array<QPointF, 4>;

struct ArCapabilities {
    bool available = false; // ARKit world tracking
    bool lidar = false;     // scene depth (LiDAR): points land on any surface, more exactly
};

enum class ArMode {
    Distance, // tap points: the straight distance between each and the next
    Outline,  // tap points on one flat surface: a closed outline on it
    Box       // three corners of a box's base, then its top: width, depth, height
};

struct ArResult {
    enum class Outcome { Done, Cancelled, Failed };
    Outcome outcome = Outcome::Cancelled;
    QString error;
    ArMode mode = ArMode::Distance;
    // Distance: each segment's length (mm), point to point in space.
    std::vector<double> distancesMm;
    // Outline: the points on the surface's plane (mm; x along the first
    // side, y across it, y up as seen from above the surface).
    std::vector<QPointF> outlineMm;
    // Box: the base's two sides and the height (mm).
    double widthMm = 0;
    double depthMm = 0;
    double heightMm = 0;
};

using StopWatching = std::function<void()>;

struct MeasureDevices {
    // Starts watching the fold of the device `window` is on; the handler
    // gets the first reading and every change. Returns how to stop.
    std::function<StopWatching(QWindow* window, std::function<void(const HingeReading&)> handler)> watchHinge;
    // Gravity about 30 times a second while watched.
    std::function<StopWatching(QWindow* window, std::function<void(const GravityReading&)> handler)> watchGravity;
    // A short tap felt in the hand (1 light, 2 medium, 3 firm): a hold, a calibration done.
    std::function<void(int strength)> haptic;
    // Whether this device has a camera to take a photo with.
    std::function<bool()> cameraAvailable;
    // Shows the camera (FoldForm's own camera screen, set up by `camera`) or
    // the photo library over `window`; the chosen photo is written as an
    // upright JPEG into `folder` (at most 4096 px on its long side).
    // `anchor`: the control that asked (an iPad's popover).
    std::function<void(QWindow* window, PhotoOrigin origin, const CameraOptions& camera, const QString& folder,
                       const QRectF& anchor, std::function<void(const PhotoResult&)> finished)>
        pickPhoto;
    // The photo library, up to `maxCount` photos at once (a scan's photos); each written
    // like pickPhoto's. `finished` gets one result per photo chosen (empty: cancelled).
    std::function<void(QWindow* window, int maxCount, const QString& folder, const QRectF& anchor,
                       std::function<void(const std::vector<PhotoResult>& results)> finished)>
        pickPhotos;
    // The photo's main subject as a mask: Vision's foreground instance mask
    // (VNGenerateForegroundInstanceMaskRequest, iOS 17), all instances, written as an 8-bit
    // grayscale PNG the size of the photo (255 = subject, 0 = background) to `maskFile`.
    // Blocks: call it off the GUI thread. False when no subject was found or on failure.
    std::function<bool(const QString& photoFile, const QString& maskFile)> subjectMask;
    // The most prominent rectangle in a photo file (Vision), or none. Blocks:
    // call it off the GUI thread.
    std::function<std::optional<RectangleCorners>(const QString& file)> detectRectangle;
    std::function<ArCapabilities()> arCapabilities;
    // Shows ARKit's camera view over `window` until the owner taps Done or
    // Cancel. `lengthUnit`: "mm" or "in", for the labels shown in it.
    std::function<void(QWindow* window, ArMode mode, const QString& lengthUnit, std::function<void(const ArResult&)> finished)>
        startAr;
};

// iOS: src/ui/ios/MeasureDevicesIos.mm. Elsewhere: every function empty.
MeasureDevices platformMeasureDevices();

} // namespace os::ui
