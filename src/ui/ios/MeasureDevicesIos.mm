// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The iOS measuring hardware for the Measure tools (ui/MeasureDevices.h,
// docs/MEASURE.md): the iPhone Duo's hinge (UIHingeInteraction, iOS 27.1),
// gravity from CoreMotion turned into the window's frame, taps felt in the
// hand, whether there is a camera, and Vision's rectangle finder. The photo
// pickers are in PhotoPickerIos.mm, the AR view in ArMeasureIos.mm.
// Compiled for iOS only, with ARC (src/ui/CMakeLists.txt). The deployment
// target is iOS 17; anything newer is guarded with @available.

#include "ui/ios/MeasureIos.h"

#include <QtCore/QString>
#include <QtCore/QtGlobal>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

#include <memory>
#include <utility>
#include <vector>

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMotion/CoreMotion.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UIKit/UIKit.h>
#import <Vision/Vision.h>

#if !__has_feature(objc_arc)
#error "MeasureDevicesIos.mm is written for ARC (-fobjc-arc, see src/ui/CMakeLists.txt)"
#endif

// UIHinge and UIHingeInteraction (the iPhone Duo's fold) are in the iOS
// 27.1 SDK; an older SDK still compiles this file, without the hinge.
#if defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 270100 \
    && __has_include(<UIKit/UIHingeInteraction.h>)
#define OPENSHAPE_HAS_UIHINGE 1
#else
#define OPENSHAPE_HAS_UIHINGE 0
#endif

namespace os::ui {

namespace measureios {

namespace {

// The window the controls are in: the one with the focus, else the first
// visible one.
QWindow* appWindow()
{
    if (QWindow* focus = QGuiApplication::focusWindow())
        return focus;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows)
        if (window->isVisible())
            return window;
    return nullptr;
}

} // namespace

UIView* hostView(QWindow* window)
{
    if (!window)
        window = appWindow();
    // A QWindow's native id on iOS is its UIView (Qt's QUIView).
    return window ? (__bridge UIView*)reinterpret_cast<void*>(window->winId()) : nil;
}

UIViewController* presenterFor(UIView* view)
{
    UIViewController* controller = view.window.rootViewController;
    while (controller.presentedViewController && !controller.presentedViewController.isBeingDismissed)
        controller = controller.presentedViewController;
    return controller;
}

QString cameraDeniedMessage()
{
    return QStringLiteral("FoldForm may not use the camera. Allow it in Settings > FoldForm > Camera.");
}

} // namespace measureios

namespace {

using measureios::hostView;

// ---- Hinge (iPhone Duo)

#if OPENSHAPE_HAS_UIHINGE
// UIHingeInteraction on the Qt window's view (UIKit, iOS 27.1). Its handler
// runs on the main thread with the first state and every change; readings
// are handed on asynchronously, so the first one never arrives inside this
// call, and none after the stop function ran.
StopWatching watchHingeOn(UIView* view, std::function<void(const HingeReading&)> handler) API_AVAILABLE(ios(27.1));
StopWatching watchHingeOn(UIView* view, std::function<void(const HingeReading&)> handler)
{
    auto alive = std::make_shared<bool>(true);
    auto lastStatus = std::make_shared<int>(-1);
    const std::function<void(const HingeReading&)> deliver = std::move(handler);
    UIHingeInteraction* interaction =
        [[UIHingeInteraction alloc] initWithUpdateHandler:^(UIHingeInteraction* source, UIHingeInteractionUpdate* update) {
            (void)source;
            HingeReading reading;
            UIHinge* hinge = update.hinge;
            if (hinge) {
                reading.present = true;
                reading.angleRadians = static_cast<double>(hinge.angle);
                const NSInteger status = hinge.status;
                reading.status = status >= 0 && status <= 3 ? static_cast<HingeStatus>(static_cast<int>(status))
                                                            : HingeStatus::Unknown;
            }
            // The log tells what the Duo reports (TD-166): the first reading
            // and each change of status, not every angle.
            const int statusCode = reading.present ? static_cast<int>(reading.status) : -2;
            if (statusCode != *lastStatus) {
                *lastStatus = statusCode;
                qInfo("Measure: hinge %s, status %d, angle %.4f rad (%.1f deg)", reading.present ? "present" : "absent",
                      static_cast<int>(reading.status), reading.angleRadians, reading.angleRadians * 180.0 / 3.14159265358979323846);
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                if (*alive)
                    deliver(reading);
            });
        }];
    interaction.enabled = YES;
    [view addInteraction:interaction];
    qInfo("Measure: watching the hinge (UIHingeInteraction added)");
    // As the protocol type, so the stop function holds no iOS 27.1 class.
    id<UIInteraction> added = interaction;
    return [alive, view, added]() {
        *alive = false;
        [view removeInteraction:added];
        qInfo("Measure: stopped watching the hinge");
    };
}
#endif

bool hingeApiAvailable()
{
#if OPENSHAPE_HAS_UIHINGE
    if (@available(iOS 27.1, *))
        return true;
#endif
    return false;
}

StopWatching watchHinge(QWindow* window, std::function<void(const HingeReading&)> handler)
{
    UIView* view = hostView(window);
#if OPENSHAPE_HAS_UIHINGE
    if (@available(iOS 27.1, *)) {
        if (view)
            return watchHingeOn(view, std::move(handler));
    }
#endif
    // No view (or no hinge API): one reading that says there is no hinge.
    qInfo("Measure: no hinge to watch (%s)", view ? "the hinge API needs iOS 27.1" : "no window");
    const std::function<void(const HingeReading&)> deliver = std::move(handler);
    auto alive = std::make_shared<bool>(true);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (*alive)
            deliver(HingeReading{});
    });
    return [alive]() { *alive = false; };
}

// ---- Gravity (CoreMotion)

// One motion manager for the app, as Apple recommends.
CMMotionManager* motionManager()
{
    static CMMotionManager* manager = [[CMMotionManager alloc] init];
    return manager;
}

struct GravityWatcher {
    int key = 0;
    UIView* view; // the Qt window's view, for its scene's orientation
    std::function<void(const GravityReading&)> handler;
    bool logged = false;
};

std::vector<GravityWatcher>& gravityWatchers()
{
    static std::vector<GravityWatcher> watchers;
    return watchers;
}

UIInterfaceOrientation orientationOf(UIView* view)
{
    UIWindowScene* scene = view.window.windowScene;
    if (!scene)
        return UIInterfaceOrientationUnknown;
    return scene.effectiveGeometry.interfaceOrientation; // iOS 16
}

const char* orientationName(UIInterfaceOrientation orientation)
{
    switch (orientation) {
    case UIInterfaceOrientationPortrait:
        return "portrait";
    case UIInterfaceOrientationPortraitUpsideDown:
        return "portrait upside down";
    case UIInterfaceOrientationLandscapeLeft:
        return "landscape left";
    case UIInterfaceOrientationLandscapeRight:
        return "landscape right";
    default:
        return "unknown";
    }
}

// CoreMotion's gravity is in the device's frame: x to the right and y to
// the top of the device held in its natural portrait, z out of the screen.
// GravityReading is in the window's frame: X right on the screen as shown,
// Y down it, Z out of the screen (so Z = z in every orientation). What
// changes is which device axis points right and down on the screen:
// - Portrait: right is +x, down is -y.  X = x, Y = -y.
// - Portrait upside down: right is -x, down is +y.  X = -x, Y = y.
// - Landscape right (UIKit: the home button, or the device's bottom, on
//   the right): the device was turned a quarter turn counter-clockwise,
//   so its top points left and its right side points up. Right on the
//   screen is -y, down is -x.  X = -y, Y = -x.
//   Check: held upright, gravity in the device frame is (-1, 0, 0) (its
//   right side up), which gives Y = 1, down the screen.
// - Landscape left (the device's bottom on the left): a quarter turn
//   clockwise, its top points right and its right side down. Right on the
//   screen is +y, down is +x.  X = y, Y = x.
//   Check: held upright, gravity is (1, 0, 0), giving Y = 1.
// Lying face up: (0, 0, -1) in both frames. An unknown orientation is
// taken as portrait. `turn` says which (the level's calibration turns with it).
GravityReading toWindowFrame(CMAcceleration g, UIInterfaceOrientation orientation)
{
    GravityReading r;
    r.z = g.z;
    switch (orientation) {
    case UIInterfaceOrientationPortraitUpsideDown:
        r.x = -g.x;
        r.y = g.y;
        r.turn = 2;
        break;
    case UIInterfaceOrientationLandscapeRight:
        r.x = -g.y;
        r.y = -g.x;
        r.turn = 1;
        break;
    case UIInterfaceOrientationLandscapeLeft:
        r.x = g.y;
        r.y = g.x;
        r.turn = 3;
        break;
    default:
        r.x = g.x;
        r.y = -g.y;
        break;
    }
    return r;
}

void deliverGravity(CMDeviceMotion* motion, NSError* error)
{
    if (error) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            qInfo("Measure: device motion failed: %s", qPrintable(QString::fromNSString(error.localizedDescription)));
        }
    }
    if (!motion)
        return;
    const CMAcceleration g = motion.gravity;
    std::vector<int> keys;
    for (const GravityWatcher& w : gravityWatchers())
        keys.push_back(w.key);
    for (int key : keys) {
        // A handler may stop any watcher: look each one up again.
        std::function<void(const GravityReading&)> handler;
        GravityReading reading;
        for (GravityWatcher& w : gravityWatchers()) {
            if (w.key != key)
                continue;
            const UIInterfaceOrientation orientation = orientationOf(w.view);
            reading = toWindowFrame(g, orientation);
            if (!w.logged) {
                w.logged = true;
                qInfo("Measure: gravity in %s: device (%.3f, %.3f, %.3f) -> window (%.3f, %.3f, %.3f)",
                      orientationName(orientation), g.x, g.y, g.z, reading.x, reading.y, reading.z);
            }
            handler = w.handler;
            break;
        }
        if (handler)
            handler(reading);
    }
}

void stopGravity(int key)
{
    std::vector<GravityWatcher>& watchers = gravityWatchers();
    for (auto it = watchers.begin(); it != watchers.end(); ++it) {
        if (it->key == key) {
            watchers.erase(it);
            break;
        }
    }
    if (watchers.empty()) {
        CMMotionManager* manager = motionManager();
        if ([manager isDeviceMotionActive])
            [manager stopDeviceMotionUpdates];
        qInfo("Measure: stopped watching gravity");
    }
}

StopWatching watchGravity(QWindow* window, std::function<void(const GravityReading&)> handler)
{
    CMMotionManager* manager = motionManager();
    if (![manager isDeviceMotionAvailable]) {
        qInfo("Measure: this device has no device motion (gravity)");
        return [] {};
    }
    static int nextId = 0;
    GravityWatcher watcher;
    watcher.key = ++nextId;
    watcher.view = hostView(window);
    watcher.handler = std::move(handler);
    const int key = watcher.key;
    gravityWatchers().push_back(std::move(watcher));
    if (![manager isDeviceMotionActive]) {
        manager.deviceMotionUpdateInterval = 1.0 / 30.0;
        [manager startDeviceMotionUpdatesToQueue:[NSOperationQueue mainQueue]
                                     withHandler:^(CMDeviceMotion* motion, NSError* error) {
                                         deliverGravity(motion, error);
                                     }];
        qInfo("Measure: watching gravity (device motion at 30 Hz)");
    }
    return [key]() { stopGravity(key); };
}

// ---- Haptics

void haptic(int strength)
{
    const UIImpactFeedbackStyle style = strength <= 1 ? UIImpactFeedbackStyleLight
                                      : strength == 2 ? UIImpactFeedbackStyleMedium
                                                      : UIImpactFeedbackStyleHeavy;
    // initWithStyle: (iOS 10) is marked for a future deprecation in favour
    // of the view-based initializer (iOS 17.5); silenced here.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIImpactFeedbackGenerator* generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
#pragma clang diagnostic pop
    [generator prepare];
    [generator impactOccurred];
}

// ---- Camera

bool cameraAvailable()
{
    return [UIImagePickerController isSourceTypeAvailable:UIImagePickerControllerSourceTypeCamera];
}

// ---- Vision's rectangle finder

std::optional<RectangleCorners> findRectangle(const QString& file)
{
    NSURL* url = [NSURL fileURLWithPath:file.toNSString()];

    // The image's size in pixels and its EXIF orientation (FoldForm's
    // own photos are upright already).
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    if (!source) {
        qInfo("Measure: rectangle finder: cannot read %s", qPrintable(file));
        return std::nullopt;
    }
    NSDictionary* properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, NULL));
    CFRelease(source);
    NSNumber* widthNumber = properties[(__bridge NSString*)kCGImagePropertyPixelWidth];
    NSNumber* heightNumber = properties[(__bridge NSString*)kCGImagePropertyPixelHeight];
    NSNumber* orientationNumber = properties[(__bridge NSString*)kCGImagePropertyOrientation];
    double width = widthNumber.doubleValue;
    double height = heightNumber.doubleValue;
    if (width <= 0 || height <= 0) {
        qInfo("Measure: rectangle finder: %s has no pixel size", qPrintable(file));
        return std::nullopt;
    }
    uint32_t orientationValue = orientationNumber ? orientationNumber.unsignedIntValue : 1u;
    if (orientationValue < 1u || orientationValue > 8u)
        orientationValue = 1u;
    if (orientationValue >= 5u) // turned a quarter: Vision's coordinates are of the upright image
        std::swap(width, height);

    // A playing card or a sheet of paper lying on a table, seen at an angle.
    VNDetectRectanglesRequest* request = [[VNDetectRectanglesRequest alloc] init];
    request.minimumAspectRatio = 0.3f;
    request.maximumAspectRatio = 1.0f;
    request.minimumSize = 0.08f;
    request.quadratureTolerance = 30.0f;
    request.minimumConfidence = 0.4f;
    request.maximumObservations = 1;

    VNImageRequestHandler* handler =
        [[VNImageRequestHandler alloc] initWithURL:url
                                       orientation:static_cast<CGImagePropertyOrientation>(orientationValue)
                                           options:@{}];
    NSError* error = nil;
    if (![handler performRequests:@[ request ] error:&error]) {
        qInfo("Measure: rectangle finder failed: %s",
              error ? qPrintable(QString::fromNSString(error.localizedDescription)) : "no reason given");
        return std::nullopt;
    }
    id first = request.results.firstObject;
    if (![first isKindOfClass:[VNRectangleObservation class]]) {
        qInfo("Measure: rectangle finder found no rectangle in %s", qPrintable(file));
        return std::nullopt;
    }
    VNRectangleObservation* found = first;
    // Vision's points are normalized with the origin at the bottom left;
    // the image's pixels start at the top left.
    auto toPixels = [width, height](CGPoint p) {
        return QPointF(static_cast<double>(p.x) * width, (1.0 - static_cast<double>(p.y)) * height);
    };
    RectangleCorners corners = {{toPixels(found.topLeft), toPixels(found.topRight), toPixels(found.bottomRight),
                                 toPixels(found.bottomLeft)}};
    qInfo("Measure: rectangle found (confidence %.2f) in a %.0f x %.0f image", static_cast<double>(found.confidence),
          width, height);
    return corners;
}

// Called off the GUI thread (MeasureDevices::detectRectangle), where there
// may be no autorelease pool.
std::optional<RectangleCorners> detectRectangle(const QString& file)
{
    std::optional<RectangleCorners> corners;
    @autoreleasepool {
        corners = findRectangle(file);
    }
    return corners;
}

// ---- Vision's subject mask

// The subject of a photo as an 8-bit grayscale PNG the size of the photo
// (255 subject, 0 background). Vision's foreground instance mask (iOS 17)
// labels each pixel with its instance (0: background) at a lower
// resolution; every instance becomes 255 and the mask is scaled up to the
// photo with smooth interpolation. The mask is read from the observation's
// instanceMask property (owned by the observation), not from a generated
// buffer, so no Core Video ownership is in question.
bool findSubjectMask(const QString& photoFile, const QString& maskFile)
{
    if (@available(iOS 17.0, *)) {
        NSURL* url = [NSURL fileURLWithPath:photoFile.toNSString()];
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
        if (!source) {
            qInfo("Measure: subject mask: cannot read %s", qPrintable(photoFile));
            return false;
        }
        NSDictionary* properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, NULL));
        CFRelease(source);
        NSNumber* widthNumber = properties[(__bridge NSString*)kCGImagePropertyPixelWidth];
        NSNumber* heightNumber = properties[(__bridge NSString*)kCGImagePropertyPixelHeight];
        NSNumber* orientationNumber = properties[(__bridge NSString*)kCGImagePropertyOrientation];
        size_t width = static_cast<size_t>(widthNumber.unsignedLongValue);
        size_t height = static_cast<size_t>(heightNumber.unsignedLongValue);
        uint32_t orientationValue = orientationNumber ? orientationNumber.unsignedIntValue : 1u;
        if (orientationValue < 1u || orientationValue > 8u)
            orientationValue = 1u;
        if (orientationValue >= 5u) // turned a quarter: the mask is of the upright image
            std::swap(width, height);
        if (width == 0 || height == 0) {
            qInfo("Measure: subject mask: %s has no pixel size", qPrintable(photoFile));
            return false;
        }

        VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithURL:url
                                           orientation:static_cast<CGImagePropertyOrientation>(orientationValue)
                                               options:@{}];
        VNGenerateForegroundInstanceMaskRequest* request = [[VNGenerateForegroundInstanceMaskRequest alloc] init];
        NSError* error = nil;
        if (![handler performRequests:@[ request ] error:&error]) {
            qInfo("Measure: subject mask failed: %s",
                  error ? qPrintable(QString::fromNSString(error.localizedDescription)) : "no reason given");
            return false;
        }
        id first = request.results.firstObject;
        if (![first isKindOfClass:[VNInstanceMaskObservation class]]) {
            qInfo("Measure: subject mask: no subject in %s", qPrintable(photoFile));
            return false;
        }
        VNInstanceMaskObservation* observation = first;
        const NSUInteger instances = observation.allInstances.count;
        if (instances == 0) {
            qInfo("Measure: subject mask: no subject in %s", qPrintable(photoFile));
            return false;
        }

        // The instance labels (one byte per pixel) -> 0 or 255.
        CVPixelBufferRef labels = observation.instanceMask;
        if (!labels || CVPixelBufferGetPixelFormatType(labels) != kCVPixelFormatType_OneComponent8) {
            qInfo("Measure: subject mask: unexpected mask format 0x%08x",
                  labels ? static_cast<unsigned>(CVPixelBufferGetPixelFormatType(labels)) : 0u);
            return false;
        }
        CVPixelBufferLockBaseAddress(labels, kCVPixelBufferLock_ReadOnly);
        const size_t maskWidth = CVPixelBufferGetWidth(labels);
        const size_t maskHeight = CVPixelBufferGetHeight(labels);
        const size_t stride = CVPixelBufferGetBytesPerRow(labels);
        const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(labels));
        std::vector<uint8_t> binary(maskWidth * maskHeight, 0);
        size_t subjectPixels = 0;
        if (base) {
            for (size_t y = 0; y < maskHeight; ++y) {
                const uint8_t* row = base + y * stride;
                for (size_t x = 0; x < maskWidth; ++x) {
                    if (row[x] != 0) {
                        binary[y * maskWidth + x] = 255;
                        ++subjectPixels;
                    }
                }
            }
        }
        CVPixelBufferUnlockBaseAddress(labels, kCVPixelBufferLock_ReadOnly);
        if (subjectPixels == 0) {
            qInfo("Measure: subject mask: the mask is empty");
            return false;
        }

        // Scaled up to the photo's size.
        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, binary.data(), binary.size(), NULL);
        CGImageRef small = provider ? CGImageCreate(maskWidth, maskHeight, 8, 8, maskWidth, gray,
                                                    static_cast<CGBitmapInfo>(kCGImageAlphaNone), provider, NULL,
                                                    true, kCGRenderingIntentDefault)
                                    : NULL;
        CGContextRef context =
            CGBitmapContextCreate(NULL, width, height, 8, 0, gray, static_cast<uint32_t>(kCGImageAlphaNone));
        CGImageRef full = NULL;
        if (small && context) {
            CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
            CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                               small);
            full = CGBitmapContextCreateImage(context);
        }
        if (context)
            CGContextRelease(context);
        if (small)
            CGImageRelease(small);
        if (provider)
            CGDataProviderRelease(provider);
        CGColorSpaceRelease(gray);
        if (!full) {
            qInfo("Measure: subject mask: could not scale the mask");
            return false;
        }

        NSString* maskPath = maskFile.toNSString();
        [[NSFileManager defaultManager] createDirectoryAtPath:[maskPath stringByDeletingLastPathComponent]
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        NSURL* maskUrl = [NSURL fileURLWithPath:maskPath];
        CGImageDestinationRef destination =
            CGImageDestinationCreateWithURL((__bridge CFURLRef)maskUrl, CFSTR("public.png"), 1, NULL);
        bool written = false;
        if (destination) {
            CGImageDestinationAddImage(destination, full, NULL);
            written = CGImageDestinationFinalize(destination);
            CFRelease(destination);
        }
        CGImageRelease(full);
        qInfo("Measure: subject mask %s: %lu instance(s), %lu x %lu mask -> %lu x %lu PNG %s",
              written ? "written" : "NOT written", static_cast<unsigned long>(instances),
              static_cast<unsigned long>(maskWidth), static_cast<unsigned long>(maskHeight),
              static_cast<unsigned long>(width), static_cast<unsigned long>(height), qPrintable(maskFile));
        return written;
    }
    qInfo("Measure: subject mask needs iOS 17");
    return false;
}

// Called off the GUI thread (MeasureDevices::subjectMask), where there may
// be no autorelease pool.
bool subjectMask(const QString& photoFile, const QString& maskFile)
{
    bool written = false;
    @autoreleasepool {
        written = findSubjectMask(photoFile, maskFile);
    }
    return written;
}

} // namespace

MeasureDevices platformMeasureDevices()
{
    MeasureDevices devices;
    if (hingeApiAvailable())
        devices.watchHinge = watchHinge;
    devices.watchGravity = watchGravity;
    devices.haptic = haptic;
    devices.cameraAvailable = cameraAvailable;
    devices.pickPhoto = [](QWindow* window, PhotoOrigin origin, const QString& folder, const QRectF& anchor,
                           std::function<void(const PhotoResult&)> finished) {
        measureios::pickPhoto(window, origin, folder, anchor, std::move(finished));
    };
    devices.pickPhotos = [](QWindow* window, int maxCount, const QString& folder, const QRectF& anchor,
                            std::function<void(const std::vector<PhotoResult>&)> finished) {
        measureios::pickPhotos(window, maxCount, folder, anchor, std::move(finished));
    };
    devices.detectRectangle = detectRectangle;
    devices.subjectMask = subjectMask;
    devices.arCapabilities = measureios::arCapabilities;
    devices.startAr = [](QWindow* window, ArMode mode, const QString& lengthUnit,
                         std::function<void(const ArResult&)> finished) {
        measureios::startAr(window, mode, lengthUnit, std::move(finished));
    };
    return devices;
}

} // namespace os::ui
