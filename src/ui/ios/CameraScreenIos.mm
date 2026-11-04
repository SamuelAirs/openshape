// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// FoldForm's own camera screen for Measure's Take photo (Photo and Scan;
// docs/MEASURE.md, docs/SCAN.md), in place of the system camera: a full-
// screen UIKit view controller with AVFoundation's preview
// (AVCaptureVideoPreviewLayer, the whole 4:3 frame shown:
// AVLayerVideoGravityResizeAspect) and photo output (AVCapturePhotoOutput).
// On it, from CoreMotion's gravity (MeasureDevicesIos.mm):
// - Photo: a level for a photo from straight above. A fixed white cross
//   marks the frame's middle and a yellow one the spot straight below the
//   lens; they meet, and turn green, within 1 degree of straight down.
// - Scan: the camera's angle from straight down against Scan's band (35 to
//   55 degrees, green; 70 to 80 for the few low photos).
// - The session's previous photo as a faint overlay (a toggle), drawn in
//   the rectangle the preview's frame fills (computed here, the same one the
//   preview layer uses), so the card can stay still while the object moves.
// - 1x and 2x (and the telephoto lens where there is one), a shutter,
//   Cancel; the flash off.
// The photo comes back as the camera encoded it, its EXIF orientation that
// of the preview as shown (the screen's interface orientation), with its
// 35 mm focal length (the digital zoom considered); PhotoPickerIos.mm writes
// it as the upright JPEG Photo and Scan read. UIKit work on the main thread;
// the capture session configured, started and stopped on its own serial
// queue. Checked only on a device (TD-180). Compiled for iOS only, with ARC
// (src/ui/CMakeLists.txt).

#include "ui/ios/MeasureIos.h"

#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#if !__has_feature(objc_arc)
#error "CameraScreenIos.mm is written for ARC (-fobjc-arc, see src/ui/CMakeLists.txt)"
#endif

using os::ui::CameraOptions;
using os::ui::GravityReading;
using os::ui::StopWatching;
using os::ui::measureios::CameraShotDone;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Photo: straight down within this many degrees is green.
constexpr double kStraightDownDegrees = 1.0;
// Leaving green again only past this (no flicker at the edge).
constexpr double kStraightDownLeaveDegrees = 1.5;
// Scan's band, in degrees from straight down (docs/SCAN.md: 30 to 60 degrees
// above the table, about 45), and the low photos (10 to 20 degrees above).
constexpr double kScanBandFrom = 35;
constexpr double kScanBandTo = 55;
constexpr double kScanLowFrom = 70;
constexpr double kScanLowTo = 80;
// The moving cross: points per degree of tilt, and how far it may go.
constexpr CGFloat kCrossPointsPerDegree = 8;
constexpr CGFloat kCrossMaxOffset = 150;
// The photo's long side: the largest size the camera offers up to this
// (12 MP, 4032 px, not a 48 MP sensor's 8064: PhotoPickerIos.mm writes at
// most 4096).
constexpr int32_t kLongestPhoto = 4224;
// The overlay is drawn from a copy at most this many pixels long.
constexpr CGFloat kOverlayPixels = 2048;
constexpr CGFloat kOverlayAlpha = 0.4;

// Remembered while the app runs: the overlay was on last time.
bool gOverlayOn = false;

// A lens and its zoom factor, as one of the zoom buttons.
struct ZoomStop {
    AVCaptureDevice* device = nil;
    CGFloat factor = 1;
    NSString* label = nil;
};

// The angle between the back camera's axis (out of the back of the device:
// -z in the window's frame) and straight down (gravity), in degrees: 0 lying
// face up, 90 held upright.
double angleFromStraightDown(const GravityReading& g)
{
    const double length = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
    if (length < 1e-6)
        return 0;
    return std::acos(std::clamp(-g.z / length, -1.0, 1.0)) * 180.0 / kPi;
}

// The back camera's rotation (AVCaptureConnection.videoRotationAngle, iOS
// 17) that shows its frame upright in an interface orientation: its sensor
// is upright with the device's bottom on the right (landscape right).
CGFloat rotationAngleFor(UIInterfaceOrientation orientation)
{
    switch (orientation) {
    case UIInterfaceOrientationLandscapeRight:
        return 0;
    case UIInterfaceOrientationLandscapeLeft:
        return 180;
    case UIInterfaceOrientationPortraitUpsideDown:
        return 270;
    default:
        return 90;
    }
}

const char* lensName(AVCaptureDevice* device)
{
    if ([device.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInTelephotoCamera])
        return "telephoto";
    if ([device.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInWideAngleCamera])
        return "wide";
    return "other";
}

// The 35 mm equivalent focal length of a lens whose 4:3 frame spans
// `horizontalFov` degrees on its long side (docs/SCAN.md: the 35 mm
// equivalent relates to the frame's diagonal, 43.27 mm, which is 5/4 of
// a 4:3 frame's long side); 0 when unknown.
double focal35FromFov(double horizontalFov)
{
    if (horizontalFov <= 1 || horizontalFov >= 179)
        return 0;
    return 43.27 / (2.0 * 1.25 * std::tan(horizontalFov * kPi / 360.0));
}

// The photo's 35 mm focal length from EXIF's FocalLenIn35mmFilm
// (`reported`), which may or may not count a digital zoom `zoom`: of
// `reported` and `reported x zoom`, the one nearer what the lens's field of
// view gives at that zoom (`expected`). 0 when EXIF has none (Scan then
// estimates it from the paper).
double focalForZoom(double reported, double zoom, double expected)
{
    if (reported <= 0)
        return 0;
    if (zoom <= 1.01 || expected <= 0)
        return reported;
    const double zoomed = reported * zoom;
    return std::fabs(zoomed - expected) < std::fabs(reported - expected) ? zoomed : reported;
}

double exifNumber(NSDictionary* properties, CFStringRef key)
{
    id exif = properties[(__bridge NSString*)kCGImagePropertyExifDictionary];
    if (![exif isKindOfClass:[NSDictionary class]])
        return 0;
    id value = ((NSDictionary*)exif)[(__bridge NSString*)key];
    return [value isKindOfClass:[NSNumber class]] ? ((NSNumber*)value).doubleValue : 0;
}

NSString* reasonOf(NSError* error)
{
    NSString* reason = error.localizedDescription;
    return reason.length > 0 ? reason : @"no reason given";
}

NSDictionary* propertiesOf(NSData* data)
{
    CGImageSourceRef source = data.length > 0 ? CGImageSourceCreateWithData((__bridge CFDataRef)data, NULL) : NULL;
    if (!source)
        return nil;
    NSDictionary* properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, NULL));
    CFRelease(source);
    return properties;
}

// The previous photo for the overlay: upright (FoldForm wrote it so), at
// most kOverlayPixels long. Nil when it cannot be read. Any thread.
UIImage* overlayImageFrom(NSString* path)
{
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], NULL);
    if (!source)
        return nil;
    NSDictionary* options = @{
        (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
        (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform : @YES,
        (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize : @(static_cast<double>(kOverlayPixels)),
    };
    CGImageRef cgImage = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
    CFRelease(source);
    if (!cgImage)
        return nil;
    UIImage* image = [UIImage imageWithCGImage:cgImage];
    CGImageRelease(cgImage);
    return image;
}

// A cross of four arms around a gap, in a size x size box.
UIBezierPath* crossPath(CGFloat size, CGFloat gap, bool dot)
{
    const CGFloat c = size / 2;
    UIBezierPath* path = [UIBezierPath bezierPath];
    [path moveToPoint:CGPointMake(0, c)];
    [path addLineToPoint:CGPointMake(c - gap, c)];
    [path moveToPoint:CGPointMake(c + gap, c)];
    [path addLineToPoint:CGPointMake(size, c)];
    [path moveToPoint:CGPointMake(c, 0)];
    [path addLineToPoint:CGPointMake(c, c - gap)];
    [path moveToPoint:CGPointMake(c, c + gap)];
    [path addLineToPoint:CGPointMake(c, size)];
    if (dot)
        [path appendPath:[UIBezierPath bezierPathWithArcCenter:CGPointMake(c, c) radius:3 startAngle:0 endAngle:2 * kPi clockwise:YES]];
    return path;
}

UIColor* panelColor()
{
    return [UIColor colorWithWhite:0.08 alpha:0.72];
}

UIColor* goodColor()
{
    return [UIColor colorWithRed:0.30 green:0.85 blue:0.39 alpha:1.0];
}

UIColor* liveColor()
{
    return [UIColor colorWithRed:1.0 green:0.82 blue:0.25 alpha:1.0];
}

void tapFelt()
{
    // initWithStyle: is marked for a future deprecation (iOS 17.5's
    // view-based initializer is newer than the deployment target).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIImpactFeedbackGenerator* generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
#pragma clang diagnostic pop
    [generator impactOccurred];
}

} // namespace

// The preview: a view whose layer is AVFoundation's preview layer.
@interface OSCameraPreviewView : UIView
@property (nonatomic, readonly) AVCaptureVideoPreviewLayer* previewLayer;
@end

@implementation OSCameraPreviewView
+ (Class)layerClass
{
    return [AVCaptureVideoPreviewLayer class];
}
- (AVCaptureVideoPreviewLayer*)previewLayer
{
    return (AVCaptureVideoPreviewLayer*)self.layer;
}
@end

// A view drawn by its own shape layer (the crosses): its properties change
// without implicit animations.
@interface OSCameraShapeView : UIView
@property (nonatomic, readonly) CAShapeLayer* shape;
@end

@implementation OSCameraShapeView
+ (Class)layerClass
{
    return [CAShapeLayer class];
}
- (CAShapeLayer*)shape
{
    return (CAShapeLayer*)self.layer;
}
@end

// Scan's gauge: 0 to 90 degrees from straight down, the band in green, the
// low photos' band fainter, a marker at the camera's angle.
@interface OSCameraAngleGauge : UIView
@property (nonatomic) double angle;
@end

@implementation OSCameraAngleGauge

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.opaque = NO;
        self.backgroundColor = UIColor.clearColor;
        self.userInteractionEnabled = NO;
        self.contentMode = UIViewContentModeRedraw;
    }
    return self;
}

- (CGSize)intrinsicContentSize
{
    return CGSizeMake(260, 40);
}

- (void)setAngle:(double)angle
{
    _angle = angle;
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
    (void)rect;
    const CGRect bounds = self.bounds;
    const CGFloat left = 10;
    const CGFloat width = std::max<CGFloat>(10, bounds.size.width - 20);
    const CGFloat top = 6;
    const CGFloat height = 14;
    auto xOf = [left, width](double degrees) {
        return left + width * static_cast<CGFloat>(std::clamp(degrees, 0.0, 90.0) / 90.0);
    };
    [panelColor() setFill];
    [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(left - 4, top - 4, width + 8, height + 8) cornerRadius:9] fill];
    [[goodColor() colorWithAlphaComponent:0.9] setFill];
    UIRectFill(CGRectMake(xOf(kScanBandFrom), top, xOf(kScanBandTo) - xOf(kScanBandFrom), height));
    [[goodColor() colorWithAlphaComponent:0.45] setFill];
    UIRectFill(CGRectMake(xOf(kScanLowFrom), top, xOf(kScanLowTo) - xOf(kScanLowFrom), height));

    NSDictionary* attributes = @{
        NSFontAttributeName : [UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightMedium],
        NSForegroundColorAttributeName : UIColor.whiteColor,
    };
    for (int degrees : {0, 45, 90}) {
        NSString* text = [NSString stringWithFormat:@"%d°", degrees];
        const CGSize size = [text sizeWithAttributes:attributes];
        const CGFloat x = std::max<CGFloat>(0, std::min<CGFloat>(xOf(degrees) - size.width / 2, bounds.size.width - size.width));
        [text drawAtPoint:CGPointMake(x, top + height + 6) withAttributes:attributes];
    }

    const CGFloat x = xOf(_angle);
    UIBezierPath* marker = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(x - 2.5, top - 5, 5, height + 10) cornerRadius:2.5];
    [UIColor.whiteColor setFill];
    [marker fill];
    [[UIColor colorWithWhite:0 alpha:0.6] setStroke];
    marker.lineWidth = 1;
    [marker stroke];
}

@end

@interface OSCameraScreenController : UIViewController <AVCapturePhotoCaptureDelegate>
- (instancetype)initWithOptions:(const CameraOptions&)options done:(const CameraShotDone&)done;
@end

@implementation OSCameraScreenController {
    BOOL _scan;
    NSString* _overlayPath; // nil: no overlay offered
    CameraShotDone _done;
    BOOL _finished;

    // The capture session: configured, started and stopped on _sessionQueue
    // (as are _videoInput and the devices' zoom).
    dispatch_queue_t _sessionQueue;
    AVCaptureSession* _session;
    AVCapturePhotoOutput* _photoOutput;
    AVCaptureDeviceInput* _videoInput; // the session queue's

    // The main thread's.
    std::vector<ZoomStop> _stops;
    size_t _stopIndex;
    BOOL _running;
    BOOL _interrupted;
    BOOL _capturing;
    CGSize _photoSize;       // the photo's size as the sensor gives it (landscape), for the preview's frame
    CGFloat _rotationAngle;  // the preview's and the photo's rotation
    BOOL _rotationApplied;
    double _angle;           // degrees from straight down, smoothed
    BOOL _hasGravity;
    GravityReading _gravity; // smoothed
    BOOL _good;
    StopWatching _stopGravity;
    NSMutableArray* _observers;
    double _shotAngle;
    double _shotFov;
    double _shotZoom;
    NSString* _shotLabel;
    BOOL _wideLayout;
    BOOL _layoutChosen;

    OSCameraPreviewView* _previewView;
    UIImageView* _overlayView;
    OSCameraShapeView* _fixedCross;
    OSCameraShapeView* _movingCross;
    OSCameraAngleGauge* _gauge;
    UILabel* _readout;
    UIView* _readoutPill;
    UILabel* _instruction;
    UIView* _instructionPill;
    UIStackView* _bar;
    UIStackView* _zoomStack;
    NSMutableArray<UIButton*>* _zoomButtons;
    UIButton* _cancelButton;
    UIButton* _shutter;
    UIButton* _overlayButton;
    NSArray<NSLayoutConstraint*>* _tallConstraints;
    NSArray<NSLayoutConstraint*>* _wideConstraints;
}

- (instancetype)initWithOptions:(const CameraOptions&)options done:(const CameraShotDone&)done
{
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _scan = options.purpose == CameraOptions::Purpose::Scan;
        _overlayPath = options.overlayFile.isEmpty() ? nil : options.overlayFile.toNSString();
        _done = done;
        _finished = NO;
        _sessionQueue = dispatch_queue_create("FoldForm.measure.camera", DISPATCH_QUEUE_SERIAL);
        _session = [[AVCaptureSession alloc] init];
        _photoOutput = [[AVCapturePhotoOutput alloc] init];
        _stopIndex = 0;
        _running = NO;
        _interrupted = NO;
        _capturing = NO;
        _photoSize = CGSizeMake(4, 3);
        _rotationAngle = 90;
        _rotationApplied = NO;
        _angle = 0;
        _hasGravity = NO;
        _good = NO;
        _observers = [[NSMutableArray alloc] init];
        _shotAngle = 0;
        _shotFov = 0;
        _shotZoom = 1;
        _wideLayout = NO;
        _layoutChosen = NO;
        _zoomButtons = [[NSMutableArray alloc] init];
        [self findStops];
    }
    return self;
}

// The zoom buttons: the wide lens at 1x and 2x, or the telephoto as 2x
// where it is one (older Pro phones), and the telephoto at its own factor
// (3x, 5x) where there is one.
- (void)findStops
{
    AVCaptureDevice* wide = [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionBack];
    AVCaptureDevice* tele = [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInTelephotoCamera
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionBack];
    if (!wide) {
        wide = tele;
        tele = nil;
    }
    if (!wide)
        return;
    _stops.push_back({wide, 1, @"1×"});
    double teleRatio = 0;
    if (tele) {
        const double wideTan = std::tan(static_cast<double>(wide.activeFormat.videoFieldOfView) * kPi / 360.0);
        const double teleTan = std::tan(static_cast<double>(tele.activeFormat.videoFieldOfView) * kPi / 360.0);
        if (wideTan > 0 && teleTan > 0)
            teleRatio = wideTan / teleTan;
    }
    const bool teleIsTwo = teleRatio > 1.7 && teleRatio < 2.3;
    if (teleIsTwo)
        _stops.push_back({tele, 1, @"2×"});
    else if (wide.activeFormat.videoMaxZoomFactor >= 2)
        _stops.push_back({wide, 2, @"2×"});
    if (tele && !teleIsTwo && teleRatio >= 2.3) {
        const double rounded = std::round(teleRatio);
        NSString* label = std::fabs(teleRatio - rounded) < 0.35 ? [NSString stringWithFormat:@"%.0f×", rounded]
                                                                : [NSString stringWithFormat:@"%.1f×", teleRatio];
        _stops.push_back({tele, 1, label});
    }
    NSMutableArray<NSString*>* labels = [[NSMutableArray alloc] init];
    for (const ZoomStop& stop : _stops)
        [labels addObject:[NSString stringWithFormat:@"%@ (%s)", stop.label, lensName(stop.device)]];
    qInfo("Measure: camera: zoom buttons %s (telephoto %.2fx the wide lens)",
          [labels componentsJoinedByString:@", "].UTF8String, teleRatio);
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    // As the app's Info.plist allows (UIKit keeps only those).
    return UIInterfaceOrientationMaskAll;
}

// ---- Layout

- (UIView*)pillAround:(UILabel*)label
{
    UIView* pill = [[UIView alloc] initWithFrame:CGRectZero];
    pill.translatesAutoresizingMaskIntoConstraints = NO;
    pill.backgroundColor = panelColor();
    pill.layer.cornerRadius = 14;
    pill.userInteractionEnabled = NO;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [pill addSubview:label];
    [NSLayoutConstraint activateConstraints:@[
        [label.leadingAnchor constraintEqualToAnchor:pill.leadingAnchor constant:14],
        [label.trailingAnchor constraintEqualToAnchor:pill.trailingAnchor constant:-14],
        [label.topAnchor constraintEqualToAnchor:pill.topAnchor constant:7],
        [label.bottomAnchor constraintEqualToAnchor:pill.bottomAnchor constant:-7],
    ]];
    return pill;
}

- (UIButton*)sideButtonTitled:(NSString*)title symbol:(NSString*)symbol action:(SEL)action
{
    UIButtonConfiguration* configuration = [UIButtonConfiguration filledButtonConfiguration];
    configuration.title = title;
    configuration.image = [UIImage systemImageNamed:symbol];
    configuration.imagePlacement = NSDirectionalRectEdgeTop;
    configuration.imagePadding = 4;
    configuration.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    configuration.baseBackgroundColor = panelColor();
    configuration.baseForegroundColor = UIColor.whiteColor;
    configuration.contentInsets = NSDirectionalEdgeInsetsMake(8, 6, 8, 6);
    UIButton* button = [UIButton buttonWithConfiguration:configuration primaryAction:nil];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.titleLabel.adjustsFontSizeToFitWidth = YES;
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [NSLayoutConstraint activateConstraints:@[
        [button.widthAnchor constraintEqualToConstant:96],
        [button.heightAnchor constraintGreaterThanOrEqualToConstant:56],
    ]];
    return button;
}

- (UIButton*)zoomButtonTitled:(NSString*)title tag:(NSInteger)tag
{
    UIButtonConfiguration* configuration = [UIButtonConfiguration filledButtonConfiguration];
    configuration.title = title;
    configuration.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    configuration.baseBackgroundColor = panelColor();
    configuration.baseForegroundColor = UIColor.whiteColor;
    configuration.contentInsets = NSDirectionalEdgeInsetsMake(6, 8, 6, 8);
    UIButton* button = [UIButton buttonWithConfiguration:configuration primaryAction:nil];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.tag = tag;
    button.accessibilityLabel = [NSString stringWithFormat:@"Zoom %@", title];
    [button addTarget:self action:@selector(zoomTapped:) forControlEvents:UIControlEventTouchUpInside];
    [NSLayoutConstraint activateConstraints:@[
        [button.widthAnchor constraintEqualToConstant:52],
        [button.heightAnchor constraintEqualToConstant:44],
    ]];
    return button;
}

- (UIView*)makeShutter
{
    // A white disc in a white ring, 76 points across.
    UIButtonConfiguration* configuration = [UIButtonConfiguration filledButtonConfiguration];
    configuration.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    configuration.baseBackgroundColor = UIColor.whiteColor;
    UIButton* button = [UIButton buttonWithConfiguration:configuration primaryAction:nil];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.accessibilityLabel = @"Take photo";
    [button addTarget:self action:@selector(shutterTapped) forControlEvents:UIControlEventTouchUpInside];
    UIView* ring = [[UIView alloc] initWithFrame:CGRectZero];
    ring.translatesAutoresizingMaskIntoConstraints = NO;
    ring.userInteractionEnabled = NO;
    ring.layer.borderColor = UIColor.whiteColor.CGColor;
    ring.layer.borderWidth = 4;
    ring.layer.cornerRadius = 38;
    UIView* holder = [[UIView alloc] initWithFrame:CGRectZero];
    holder.translatesAutoresizingMaskIntoConstraints = NO;
    [holder addSubview:ring];
    [holder addSubview:button];
    [NSLayoutConstraint activateConstraints:@[
        [holder.widthAnchor constraintEqualToConstant:76],
        [holder.heightAnchor constraintEqualToConstant:76],
        [ring.leadingAnchor constraintEqualToAnchor:holder.leadingAnchor],
        [ring.trailingAnchor constraintEqualToAnchor:holder.trailingAnchor],
        [ring.topAnchor constraintEqualToAnchor:holder.topAnchor],
        [ring.bottomAnchor constraintEqualToAnchor:holder.bottomAnchor],
        [button.centerXAnchor constraintEqualToAnchor:holder.centerXAnchor],
        [button.centerYAnchor constraintEqualToAnchor:holder.centerYAnchor],
        [button.widthAnchor constraintEqualToConstant:62],
        [button.heightAnchor constraintEqualToConstant:62],
    ]];
    _shutter = button;
    return holder; // placed by its holder; _shutter is the control
}

- (OSCameraShapeView*)crossWithColor:(UIColor*)color width:(CGFloat)width dot:(bool)dot
{
    OSCameraShapeView* view = [[OSCameraShapeView alloc] initWithFrame:CGRectMake(0, 0, 60, 60)];
    view.userInteractionEnabled = NO;
    view.backgroundColor = UIColor.clearColor;
    view.shape.path = crossPath(60, 9, dot).CGPath;
    view.shape.fillColor = dot ? color.CGColor : UIColor.clearColor.CGColor;
    view.shape.strokeColor = color.CGColor;
    view.shape.lineWidth = width;
    view.shape.lineCap = kCALineCapRound;
    view.shape.shadowColor = UIColor.blackColor.CGColor;
    view.shape.shadowOpacity = 0.6f;
    view.shape.shadowRadius = 1.5;
    view.shape.shadowOffset = CGSizeZero;
    return view;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.blackColor;
    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;

    // The preview, inside the safe area; the whole frame shown.
    _previewView = [[OSCameraPreviewView alloc] initWithFrame:CGRectZero];
    _previewView.translatesAutoresizingMaskIntoConstraints = NO;
    _previewView.backgroundColor = UIColor.blackColor;
    _previewView.previewLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    _previewView.previewLayer.session = _session;
    [self.view addSubview:_previewView];

    // The overlay, the crosses: placed on the preview's frame in
    // viewDidLayoutSubviews.
    _overlayView = [[UIImageView alloc] initWithFrame:CGRectZero];
    _overlayView.contentMode = UIViewContentModeScaleAspectFit;
    _overlayView.clipsToBounds = YES;
    _overlayView.alpha = kOverlayAlpha;
    _overlayView.hidden = YES;
    _overlayView.userInteractionEnabled = NO;
    [self.view addSubview:_overlayView];

    _fixedCross = [self crossWithColor:UIColor.whiteColor width:2 dot:false];
    _movingCross = [self crossWithColor:liveColor() width:2.5 dot:true];
    _fixedCross.hidden = YES;
    _movingCross.hidden = YES;
    [self.view addSubview:_fixedCross];
    [self.view addSubview:_movingCross];

    _readout = [[UILabel alloc] initWithFrame:CGRectZero];
    _readout.font = [UIFont monospacedDigitSystemFontOfSize:22 weight:UIFontWeightSemibold];
    _readout.textColor = UIColor.whiteColor;
    _readout.textAlignment = NSTextAlignmentCenter;
    _readoutPill = [self pillAround:_readout];
    _readoutPill.hidden = YES;
    [self.view addSubview:_readoutPill];

    _instruction = [[UILabel alloc] initWithFrame:CGRectZero];
    _instruction.font = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium];
    _instruction.textColor = UIColor.whiteColor;
    _instruction.textAlignment = NSTextAlignmentCenter;
    _instruction.numberOfLines = 0;
    _instructionPill = [self pillAround:_instruction];
    [self.view addSubview:_instructionPill];

    _gauge = [[OSCameraAngleGauge alloc] initWithFrame:CGRectZero];
    _gauge.translatesAutoresizingMaskIntoConstraints = NO;
    _gauge.hidden = YES;
    [self.view addSubview:_gauge];

    _cancelButton = [self sideButtonTitled:@"Cancel" symbol:@"xmark" action:@selector(cancelTapped)];
    _overlayButton = [self sideButtonTitled:@"Last photo" symbol:@"square.on.square" action:@selector(overlayTapped)];
    _overlayButton.accessibilityHint = @"Shows the last photo faintly over the camera view";
    // In its place, but unseen, until there is a last photo to show.
    _overlayButton.alpha = 0;
    _overlayButton.userInteractionEnabled = NO;
    UIView* shutterHolder = [self makeShutter];
    _shutter.enabled = NO;

    _bar = [[UIStackView alloc] initWithArrangedSubviews:@[ _cancelButton, shutterHolder, _overlayButton ]];
    _bar.translatesAutoresizingMaskIntoConstraints = NO;
    _bar.alignment = UIStackViewAlignmentCenter;
    _bar.spacing = 28;
    [self.view addSubview:_bar];

    NSMutableArray<UIView*>* zoomViews = [[NSMutableArray alloc] init];
    for (size_t i = 0; i < _stops.size(); ++i) {
        UIButton* button = [self zoomButtonTitled:_stops[i].label tag:static_cast<NSInteger>(i)];
        [_zoomButtons addObject:button];
        [zoomViews addObject:button];
    }
    _zoomStack = [[UIStackView alloc] initWithArrangedSubviews:zoomViews];
    _zoomStack.translatesAutoresizingMaskIntoConstraints = NO;
    _zoomStack.alignment = UIStackViewAlignmentCenter;
    _zoomStack.spacing = 10;
    _zoomStack.hidden = _stops.size() < 2;
    [self.view addSubview:_zoomStack];

    [NSLayoutConstraint activateConstraints:@[
        [_previewView.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [_previewView.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [_previewView.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [_previewView.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],

        [_readoutPill.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_readoutPill.topAnchor constraintEqualToAnchor:safe.topAnchor constant:12],
        [_instructionPill.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_instructionPill.topAnchor constraintEqualToAnchor:_readoutPill.bottomAnchor constant:8],
        [_instructionPill.widthAnchor constraintLessThanOrEqualToAnchor:safe.widthAnchor constant:-32],
        [_instructionPill.widthAnchor constraintLessThanOrEqualToConstant:520],
        [_gauge.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_gauge.topAnchor constraintEqualToAnchor:_instructionPill.bottomAnchor constant:8],
        [_gauge.widthAnchor constraintEqualToConstant:260],
        [_gauge.heightAnchor constraintEqualToConstant:40],
    ]];
    // A tall screen: the buttons along the bottom; a wide one: down the
    // trailing side (chosen in viewWillLayoutSubviews).
    _tallConstraints = @[
        [_bar.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_bar.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16],
        [_zoomStack.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [_zoomStack.bottomAnchor constraintEqualToAnchor:_bar.topAnchor constant:-14],
    ];
    _wideConstraints = @[
        [_bar.centerYAnchor constraintEqualToAnchor:safe.centerYAnchor],
        [_bar.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-16],
        [_zoomStack.centerYAnchor constraintEqualToAnchor:safe.centerYAnchor],
        [_zoomStack.trailingAnchor constraintEqualToAnchor:_bar.leadingAnchor constant:-14],
    ];

    _instruction.text = _scan ? @"Tilt to about 45° with the whole sheet in view" : @"Hold it flat above the card";
    _gauge.hidden = !_scan;
    [self updateZoomButtons];
    [self updateOverlayButton];

    if (_overlayPath) {
        NSString* path = _overlayPath;
        __weak OSCameraScreenController* weakSelf = self;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            UIImage* image = nil;
            @autoreleasepool {
                image = overlayImageFrom(path);
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf overlayLoaded:image];
            });
        });
    }

    [self observeSession];
    dispatch_async(_sessionQueue, ^{
        [self configureSession];
    });
}

- (void)viewWillLayoutSubviews
{
    [super viewWillLayoutSubviews];
    const CGSize size = self.view.bounds.size;
    const BOOL wide = size.width > size.height;
    if (_layoutChosen && wide == _wideLayout)
        return;
    _layoutChosen = YES;
    _wideLayout = wide;
    [NSLayoutConstraint deactivateConstraints:wide ? _tallConstraints : _wideConstraints];
    [NSLayoutConstraint activateConstraints:wide ? _wideConstraints : _tallConstraints];
    _bar.axis = wide ? UILayoutConstraintAxisVertical : UILayoutConstraintAxisHorizontal;
    _zoomStack.axis = wide ? UILayoutConstraintAxisVertical : UILayoutConstraintAxisHorizontal;
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    [self applyRotation:NO];
    [self placeOnFrame];
}

- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator
{
    [super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
    [coordinator animateAlongsideTransition:nil
                                 completion:^(id<UIViewControllerTransitionCoordinatorContext> context) {
                                     (void)context;
                                     [self applyRotation:NO];
                                     [self.view setNeedsLayout];
                                 }];
}

// The rectangle the camera's whole frame fills on the screen: the preview
// layer's (AVLayerVideoGravityResizeAspect: the frame, turned as shown,
// fitted into the preview's bounds and centered).
- (CGRect)frameRect
{
    const BOOL turned = _rotationAngle == 90 || _rotationAngle == 270;
    const CGSize shown = turned ? CGSizeMake(_photoSize.height, _photoSize.width) : _photoSize;
    const CGRect inPreview = AVMakeRectWithAspectRatioInsideRect(shown, _previewView.bounds);
    return [_previewView convertRect:inPreview toView:self.view];
}

- (void)placeOnFrame
{
    const CGRect frame = [self frameRect];
    _overlayView.frame = frame;
    const CGPoint middle = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
    _fixedCross.center = middle;
    [self placeMovingCross];
}

// The yellow cross: where straight down is, seen from the frame's middle.
// Tilting the phone moves the spot below the lens toward the screen's
// "down" in gravity's terms: along gravity's part in the screen's plane.
- (void)placeMovingCross
{
    const CGRect frame = [self frameRect];
    const CGPoint middle = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
    const double along = std::hypot(_gravity.x, _gravity.y);
    const CGFloat offset = std::min<CGFloat>(static_cast<CGFloat>(_angle) * kCrossPointsPerDegree, kCrossMaxOffset);
    CGPoint at = middle;
    if (along > 1e-6) {
        at.x += static_cast<CGFloat>(_gravity.x / along) * offset;
        at.y += static_cast<CGFloat>(_gravity.y / along) * offset;
    }
    _movingCross.center = at;
}

// The preview shown upright in the screen's orientation (and the photo
// taken so: shutterTapped). `force`: after the session's connection changed.
- (void)applyRotation:(BOOL)force
{
    UIWindowScene* scene = self.view.window.windowScene;
    const CGFloat angle = scene ? rotationAngleFor(scene.effectiveGeometry.interfaceOrientation) : _rotationAngle;
    const BOOL changed = angle != _rotationAngle;
    _rotationAngle = angle;
    // No connection yet: applied when the session runs (sessionStarted).
    AVCaptureConnection* connection = _previewView.previewLayer.connection;
    if (connection && (force || changed || !_rotationApplied)) {
        if ([connection isVideoRotationAngleSupported:angle])
            connection.videoRotationAngle = angle;
        else
            qInfo("Measure: camera: the preview cannot be turned by %.0f degrees", static_cast<double>(angle));
        _rotationApplied = YES; // tried once per change: no layout loop
    }
    if (changed)
        [self.view setNeedsLayout]; // the frame's rectangle turns with it
}

- (void)updateZoomButtons
{
    for (UIButton* button in _zoomButtons) {
        UIButtonConfiguration* configuration = button.configuration;
        const BOOL selected = static_cast<size_t>(button.tag) == _stopIndex;
        configuration.baseForegroundColor = selected ? liveColor() : UIColor.whiteColor;
        button.configuration = configuration;
        button.accessibilityTraits = selected ? UIAccessibilityTraitButton | UIAccessibilityTraitSelected : UIAccessibilityTraitButton;
        button.enabled = _running && !_capturing;
    }
}

- (void)updateOverlayButton
{
    const BOOL available = _overlayView.image != nil;
    _overlayButton.alpha = available ? 1 : 0;
    _overlayButton.userInteractionEnabled = available;
    UIButtonConfiguration* configuration = _overlayButton.configuration;
    configuration.baseBackgroundColor = gOverlayOn && available ? [UIColor colorWithWhite:1 alpha:0.9] : panelColor();
    configuration.baseForegroundColor = gOverlayOn && available ? UIColor.blackColor : UIColor.whiteColor;
    _overlayButton.configuration = configuration;
    _overlayButton.accessibilityTraits =
        gOverlayOn && available ? UIAccessibilityTraitButton | UIAccessibilityTraitSelected : UIAccessibilityTraitButton;
    _overlayView.hidden = !(gOverlayOn && available);
}

- (void)overlayLoaded:(UIImage*)image
{
    if (!image) {
        qInfo("Measure: camera: the last photo could not be read for the overlay");
        return;
    }
    _overlayView.image = image;
    qInfo("Measure: camera: the last photo is ready as an overlay (%.0f x %.0f, %s)",
          static_cast<double>(image.size.width), static_cast<double>(image.size.height), gOverlayOn ? "shown" : "hidden");
    [self updateOverlayButton];
}

// ---- Appearing and going

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [UIApplication sharedApplication].idleTimerDisabled = YES;
    if (!_stopGravity) {
        __weak OSCameraScreenController* weakSelf = self;
        _stopGravity = os::ui::measureios::watchGravityIn(self.view, [weakSelf](const GravityReading& g) {
            [weakSelf gravityChanged:g];
        });
    }
}

- (void)viewDidDisappear:(BOOL)animated
{
    [super viewDidDisappear:animated];
    // Gone some other way than Cancel or the shutter: cancelled.
    if (!_finished)
        [self finishWithPhoto:nil focalLength:0 error:nil dismiss:NO];
}

// Everything stops: the gravity, the notifications, the session (on its
// queue; the block keeps the session until it has stopped). Idempotent.
- (void)stopEverything
{
    if (_stopGravity) {
        StopWatching stop = std::move(_stopGravity);
        _stopGravity = nullptr;
        stop();
    }
    for (id observer in _observers)
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
    [_observers removeAllObjects];
    [UIApplication sharedApplication].idleTimerDisabled = NO;
    _running = NO;
    AVCaptureSession* session = _session;
    dispatch_async(_sessionQueue, ^{
        if (session.isRunning)
            [session stopRunning];
    });
}

- (void)finishWithPhoto:(NSData*)photo focalLength:(double)focal error:(NSString*)error dismiss:(BOOL)dismiss
{
    if (_finished)
        return;
    _finished = YES;
    [self stopEverything];
    const CameraShotDone done = std::move(_done);
    _done = nullptr;
    qInfo("Measure: camera: %s%s%s", photo ? "photo taken" : error ? "failed" : "cancelled", error ? ": " : "",
          error ? error.UTF8String : "");
    if (dismiss)
        [self dismissSoon];
    if (done)
        done(photo, focal, error);
}

// Dismissed now, or when the presentation still running has ended (a
// failure can come before it has).
- (void)dismissSoon
{
    id<UIViewControllerTransitionCoordinator> coordinator = self.transitionCoordinator;
    if (coordinator && self.isBeingPresented) {
        [coordinator animateAlongsideTransition:nil
                                     completion:^(id<UIViewControllerTransitionCoordinatorContext> context) {
                                         (void)context;
                                         [self dismissViewControllerAnimated:YES completion:nil];
                                     }];
        return;
    }
    if (self.presentingViewController && !self.isBeingDismissed)
        [self dismissViewControllerAnimated:YES completion:nil];
}

// ---- The capture session (on _sessionQueue)

- (void)observeSession
{
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    NSOperationQueue* main = [NSOperationQueue mainQueue];
    __weak OSCameraScreenController* weakSelf = self;
    [_observers addObject:[center addObserverForName:AVCaptureSessionRuntimeErrorNotification
                                              object:_session
                                               queue:main
                                          usingBlock:^(NSNotification* note) {
                                              [weakSelf sessionFailed:note.userInfo[AVCaptureSessionErrorKey]];
                                          }]];
    [_observers addObject:[center addObserverForName:AVCaptureSessionWasInterruptedNotification
                                              object:_session
                                               queue:main
                                          usingBlock:^(NSNotification* note) {
                                              [weakSelf sessionInterrupted:note.userInfo[AVCaptureSessionInterruptionReasonKey]];
                                          }]];
    [_observers addObject:[center addObserverForName:AVCaptureSessionInterruptionEndedNotification
                                              object:_session
                                               queue:main
                                          usingBlock:^(NSNotification* note) {
                                              (void)note;
                                              [weakSelf sessionResumed];
                                          }]];
}

// The photo's largest size up to kLongestPhoto (12 MP where the sensor
// also offers 48 MP). On the session queue, after the device's format is
// set (the session's preset, a lens switched).
- (void)choosePhotoSizeFor:(AVCaptureDevice*)device
{
    CMVideoDimensions best = {0, 0};
    for (NSValue* value in device.activeFormat.supportedMaxPhotoDimensions) {
        CMVideoDimensions d = {0, 0};
        [value getValue:&d size:sizeof(d)];
        if (std::max(d.width, d.height) > kLongestPhoto)
            continue;
        if (static_cast<int64_t>(d.width) * d.height > static_cast<int64_t>(best.width) * best.height)
            best = d;
    }
    if (best.width > 0 && best.height > 0)
        _photoOutput.maxPhotoDimensions = best;
}

- (void)configureSession
{
    AVCaptureSession* session = _session;
    AVCaptureDevice* device = _stops.empty() ? nil : _stops.front().device; // set in init, not changed since
    NSString* failure = nil;
    [session beginConfiguration];
    if ([session canSetSessionPreset:AVCaptureSessionPresetPhoto])
        session.sessionPreset = AVCaptureSessionPresetPhoto;
    NSError* error = nil;
    AVCaptureDeviceInput* input = device ? [AVCaptureDeviceInput deviceInputWithDevice:device error:&error] : nil;
    if (input && [session canAddInput:input]) {
        [session addInput:input];
        _videoInput = input;
    } else {
        failure = device ? [NSString stringWithFormat:@"The camera could not be opened (%@).", reasonOf(error)]
                         : @"This device has no camera to take a photo with.";
    }
    if (!failure) {
        if ([session canAddOutput:_photoOutput])
            [session addOutput:_photoOutput];
        else
            failure = @"The camera could not take photos.";
    }
    // Split View, Stage Manager, a folding phone's split screen: the camera
    // keeps running beside another app (iOS 16).
    if (!failure && session.isMultitaskingCameraAccessSupported)
        session.multitaskingCameraAccessEnabled = YES;
    [session commitConfiguration];
    if (failure) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self finishWithPhoto:nil focalLength:0 error:failure dismiss:YES];
        });
        return;
    }
    [self choosePhotoSizeFor:device];
    [session startRunning];
    const CMVideoDimensions size = _photoOutput.maxPhotoDimensions;
    const BOOL running = session.isRunning;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self sessionStarted:running photoWidth:size.width height:size.height];
    });
}

// On the session queue: the lens and its zoom.
- (void)useDevice:(AVCaptureDevice*)device zoom:(CGFloat)factor
{
    AVCaptureSession* session = _session;
    BOOL switched = NO;
    if (_videoInput && device != _videoInput.device) {
        NSError* error = nil;
        AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
        if (input) {
            [session beginConfiguration];
            AVCaptureDeviceInput* previous = _videoInput;
            [session removeInput:previous];
            if ([session canAddInput:input]) {
                [session addInput:input];
                _videoInput = input;
                switched = YES;
            } else if ([session canAddInput:previous]) {
                [session addInput:previous];
            }
            [session commitConfiguration];
        }
        if (!switched)
            qInfo("Measure: camera: the %s lens could not be used (%s)", lensName(device),
                  error ? error.localizedDescription.UTF8String : "the session refused it");
        else
            [self choosePhotoSizeFor:device];
    }
    AVCaptureDevice* active = _videoInput.device;
    NSError* error = nil;
    if (active && [active lockForConfiguration:&error]) {
        active.videoZoomFactor = std::clamp(factor, active.minAvailableVideoZoomFactor, active.maxAvailableVideoZoomFactor);
        [active unlockForConfiguration];
    }
    const CGFloat zoom = active.videoZoomFactor;
    const CMVideoDimensions size = _photoOutput.maxPhotoDimensions;
    const char* lens = lensName(active);
    dispatch_async(dispatch_get_main_queue(), ^{
        [self lensChanged:switched zoom:zoom lens:lens photoWidth:size.width height:size.height];
    });
}

// ---- Main thread: the session's news

- (void)sessionStarted:(BOOL)running photoWidth:(int32_t)width height:(int32_t)height
{
    if (_finished)
        return;
    _running = running;
    if (width > 0 && height > 0)
        _photoSize = CGSizeMake(width, height);
    qInfo("Measure: camera: session %s (%s, %d x %d photos, %s lens)", running ? "running" : "NOT running",
          _scan ? "scan" : "photo", static_cast<int>(width), static_cast<int>(height),
          _stops.empty() ? "no" : lensName(_stops.front().device));
    [self applyRotation:YES];
    [self.view setNeedsLayout];
    [self refreshControls];
}

- (void)lensChanged:(BOOL)switched zoom:(CGFloat)zoom lens:(const char*)lens photoWidth:(int32_t)width height:(int32_t)height
{
    if (_finished)
        return;
    if (width > 0 && height > 0)
        _photoSize = CGSizeMake(width, height);
    qInfo("Measure: camera: zoom %.1fx on the %s lens%s", static_cast<double>(zoom), lens, switched ? " (switched)" : "");
    if (switched)
        [self applyRotation:YES]; // the preview's connection is a new one
    [self.view setNeedsLayout];
    [self refreshControls];
}

- (void)sessionFailed:(NSError*)error
{
    if (_finished)
        return;
    qInfo("Measure: camera: session error %ld: %s", static_cast<long>(error.code), reasonOf(error).UTF8String);
    if (error.code == AVErrorMediaServicesWereReset) {
        AVCaptureSession* session = _session;
        dispatch_async(_sessionQueue, ^{
            [session startRunning];
        });
        return;
    }
    [self finishWithPhoto:nil
              focalLength:0
                    error:[NSString stringWithFormat:@"The camera stopped (%@).", reasonOf(error)]
                  dismiss:YES];
}

- (void)sessionInterrupted:(NSNumber*)reason
{
    qInfo("Measure: camera: interrupted (reason %d)", reason ? reason.intValue : -1);
    _interrupted = YES;
    const NSInteger code = reason ? reason.integerValue : 0;
    if (code == AVCaptureSessionInterruptionReasonVideoDeviceInUseByAnotherClient)
        _instruction.text = @"Another app is using the camera.";
    else if (code == AVCaptureSessionInterruptionReasonVideoDeviceNotAvailableWithMultipleForegroundApps)
        _instruction.text = @"The camera is paused while another app is beside FoldForm.";
    else
        _instruction.text = @"The camera is paused.";
    [self refreshControls];
}

- (void)sessionResumed
{
    qInfo("Measure: camera: resumed");
    _interrupted = NO;
    _instruction.text = _scan ? @"Tilt to about 45° with the whole sheet in view" : @"Hold it flat above the card";
    [self refreshControls];
}

- (void)refreshControls
{
    _shutter.enabled = _running && !_capturing && !_interrupted;
    [self updateZoomButtons];
}

// ---- Gravity: the level and the angle

- (void)gravityChanged:(const GravityReading&)g
{
    if (_finished)
        return;
    if (!_hasGravity) {
        _gravity = g;
        _hasGravity = YES;
    } else {
        // A little smoothing against the hand's shake (30 readings a second).
        const double k = 0.35;
        _gravity.x += k * (g.x - _gravity.x);
        _gravity.y += k * (g.y - _gravity.y);
        _gravity.z += k * (g.z - _gravity.z);
    }
    _angle = angleFromStraightDown(_gravity);

    BOOL good = NO;
    BOOL low = NO;
    if (_scan) {
        good = _angle >= kScanBandFrom && _angle <= kScanBandTo;
        low = _angle >= kScanLowFrom && _angle <= kScanLowTo;
        _readout.text = low ? [NSString stringWithFormat:@"%.0f° from straight down, a low photo", _angle]
                            : [NSString stringWithFormat:@"%.0f° from straight down", _angle];
        _gauge.angle = _angle;
        good = good || low;
    } else {
        good = _angle <= (_good ? kStraightDownLeaveDegrees : kStraightDownDegrees);
        _readout.text = [NSString stringWithFormat:@"%.1f° off", _angle];
        UIColor* cross = good ? goodColor() : liveColor();
        _movingCross.shape.strokeColor = cross.CGColor;
        _movingCross.shape.fillColor = cross.CGColor;
        _fixedCross.shape.strokeColor = good ? goodColor().CGColor : UIColor.whiteColor.CGColor;
        _fixedCross.hidden = NO;
        _movingCross.hidden = NO;
        [self placeMovingCross];
    }
    _readout.textColor = good ? goodColor() : UIColor.whiteColor;
    _readoutPill.hidden = NO;
    if (good && !_good)
        tapFelt();
    _good = good;
}

// ---- Buttons

- (void)cancelTapped
{
    if (_finished) {
        [self dismissSoon]; // finished already (a failure): just go
        return;
    }
    [self finishWithPhoto:nil focalLength:0 error:nil dismiss:YES];
}

- (void)overlayTapped
{
    gOverlayOn = !gOverlayOn;
    qInfo("Measure: camera: overlay %s", gOverlayOn ? "on" : "off");
    [self updateOverlayButton];
}

- (void)zoomTapped:(UIButton*)button
{
    const size_t index = static_cast<size_t>(button.tag);
    if (index >= _stops.size() || index == _stopIndex || !_running || _capturing)
        return;
    _stopIndex = index;
    [self updateZoomButtons];
    AVCaptureDevice* device = _stops[index].device;
    const CGFloat factor = _stops[index].factor;
    dispatch_async(_sessionQueue, ^{
        [self useDevice:device zoom:factor];
    });
}

- (void)shutterTapped
{
    if (!_running || _capturing || _interrupted || _finished)
        return;
    _capturing = YES;
    [self refreshControls];
    _shotAngle = _angle;
    _shotLabel = _stopIndex < _stops.size() ? _stops[_stopIndex].label : @"1×";
    const CGFloat rotation = _rotationAngle;
    AVCapturePhotoOutput* output = _photoOutput;
    dispatch_async(_sessionQueue, ^{
        [self captureWithOutput:output rotation:rotation];
    });
}

// On the session queue: the photo, turned as the preview is shown, the
// flash off, at the size chosen.
- (void)captureWithOutput:(AVCapturePhotoOutput*)output rotation:(CGFloat)rotation
{
    AVCaptureConnection* connection = [output connectionWithMediaType:AVMediaTypeVideo];
    if (!connection || !connection.enabled || !connection.active) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self captureFailed:@"The camera is not ready yet. Try again."];
        });
        return;
    }
    if ([connection isVideoRotationAngleSupported:rotation])
        connection.videoRotationAngle = rotation;
    AVCapturePhotoSettings* settings = [output.availablePhotoCodecTypes containsObject:AVVideoCodecTypeJPEG]
                                           ? [AVCapturePhotoSettings photoSettingsWithFormat:@{AVVideoCodecKey : AVVideoCodecTypeJPEG}]
                                           : [AVCapturePhotoSettings photoSettings];
    if ([output.supportedFlashModes containsObject:@(AVCaptureFlashModeOff)])
        settings.flashMode = AVCaptureFlashModeOff;
    const CMVideoDimensions largest = output.maxPhotoDimensions;
    if (largest.width > 0 && largest.height > 0)
        settings.maxPhotoDimensions = largest;
    AVCaptureDevice* device = _videoInput.device;
    const double fov = static_cast<double>(device.activeFormat.videoFieldOfView);
    const double zoom = static_cast<double>(device.videoZoomFactor);
    // Queued before the photo's own news (the main queue keeps the order).
    dispatch_async(dispatch_get_main_queue(), ^{
        [self noteShotFov:fov zoom:zoom];
    });
    @try {
        [output capturePhotoWithSettings:settings delegate:self];
    } @catch (NSException* exception) {
        NSString* reason = exception.reason ? [exception.reason copy] : @"no reason given";
        dispatch_async(dispatch_get_main_queue(), ^{
            [self captureFailed:[NSString stringWithFormat:@"The photo could not be taken (%@).", reason]];
        });
    }
}

- (void)noteShotFov:(double)fov zoom:(double)zoom
{
    _shotFov = fov;
    _shotZoom = zoom;
}

// The shutter could not fire: the screen stays, the shutter is offered again.
- (void)captureFailed:(NSString*)message
{
    qInfo("Measure: camera: %s", message.UTF8String);
    _capturing = NO;
    _instruction.text = message;
    [self refreshControls];
}

// ---- AVCapturePhotoCaptureDelegate (any queue)

- (void)captureOutput:(AVCapturePhotoOutput*)output willCapturePhotoForResolvedSettings:(AVCaptureResolvedPhotoSettings*)resolvedSettings
{
    (void)output;
    (void)resolvedSettings;
    OSCameraPreviewView* preview = _previewView; // set once in viewDidLoad
    dispatch_async(dispatch_get_main_queue(), ^{
        // The shutter's blink.
        preview.alpha = 0.15;
        [UIView animateWithDuration:0.25
                         animations:^{
                             preview.alpha = 1;
                         }];
    });
}

- (void)captureOutput:(AVCapturePhotoOutput*)output didFinishProcessingPhoto:(AVCapturePhoto*)photo error:(NSError*)error
{
    (void)output;
    NSData* data = error ? nil : [photo fileDataRepresentation];
    NSDictionary* metadata = photo.metadata;
    NSString* reason = error ? [error.localizedDescription copy] : nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self photoArrived:data metadata:metadata error:reason];
    });
}

// ---- Main thread: the photo

- (void)photoArrived:(NSData*)data metadata:(NSDictionary*)metadata error:(NSString*)reason
{
    if (_finished)
        return;
    _capturing = NO;
    if (!data) {
        [self captureFailed:[NSString stringWithFormat:@"The camera gave no photo (%@).", reason ? reason : @"no reason given"]];
        return;
    }
    NSDictionary* properties = propertiesOf(data);
    double reported = exifNumber(properties, kCGImagePropertyExifFocalLenIn35mmFilm);
    if (reported <= 0)
        reported = exifNumber(metadata, kCGImagePropertyExifFocalLenIn35mmFilm);
    double digitalZoom = exifNumber(properties, kCGImagePropertyExifDigitalZoomRatio);
    if (digitalZoom <= 0)
        digitalZoom = exifNumber(metadata, kCGImagePropertyExifDigitalZoomRatio);
    const double expected = focal35FromFov(_shotFov) * _shotZoom;
    const double focal = focalForZoom(reported, _shotZoom, expected);
    qInfo("Measure: camera: captured at %.1f° from straight down, %s (%s), 35 mm focal length %.0f "
          "(EXIF %.0f, digital zoom %.2f; %.0f expected from the lens's field of view), %lu bytes, rotation %.0f",
          _shotAngle, _shotLabel.UTF8String, _scan ? "scan" : "photo", focal, reported, digitalZoom, expected,
          static_cast<unsigned long>(data.length), static_cast<double>(_rotationAngle));
    [self finishWithPhoto:data focalLength:focal error:nil dismiss:YES];
}

@end

namespace os::ui::measureios {

void presentCameraScreen(UIViewController* presenter, const CameraOptions& options, CameraShotDone done)
{
    OSCameraScreenController* controller = [[OSCameraScreenController alloc] initWithOptions:options done:done];
    controller.modalPresentationStyle = UIModalPresentationFullScreen;
    qInfo("Measure: camera: showing FoldForm's camera screen (%s, %s)",
          options.purpose == CameraOptions::Purpose::Scan ? "scan" : "photo",
          options.overlayFile.isEmpty() ? "no last photo" : "the last photo offered as an overlay");
    [presenter presentViewController:controller animated:YES completion:nil];
}

} // namespace os::ui::measureios
