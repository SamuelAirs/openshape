// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Photos for the Measure tools (MeasureDevices::pickPhoto and pickPhotos,
// docs/MEASURE.md): the camera (UIImagePickerController, after asking for
// the camera where iOS has not yet) or the photo library
// (PHPickerViewController, iOS 14, which runs outside the app and needs no
// permission; one photo, or several for a scan). Each photo is drawn
// upright at most 4096 px on its long side and written as a JPEG into the
// folder asked for, with the original's 35 mm focal length read from its
// EXIF first; `finished` runs once, on the main thread. On the iPhone Duo
// the system's document picker showed no files (TD-158), so each step is
// logged to learn whether the photo picker works there. Compiled for iOS
// only, with ARC (src/ui/CMakeLists.txt).

#include "ui/ios/MeasureIos.h"

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <PhotosUI/PhotosUI.h>
#import <UIKit/UIKit.h>

#if !__has_feature(objc_arc)
#error "PhotoPickerIos.mm is written for ARC (-fobjc-arc, see src/ui/CMakeLists.txt)"
#endif

using os::ui::PhotoResult;

namespace {

constexpr CGFloat kMaxPixels = 4096;

using PhotoDone = std::function<void(const PhotoResult&)>;

QString qtText(NSString* string)
{
    return string ? QString::fromNSString(string) : QString();
}

PhotoResult failed(NSString* message)
{
    PhotoResult result;
    result.outcome = PhotoResult::Outcome::Failed;
    result.error = qtText(message);
    return result;
}

// The 35 mm equivalent focal length in an image's properties (ImageIO's
// or the camera's metadata dictionary: {Exif} -> FocalLenIn35mmFilm); 0
// when it is not there.
double focalLengthIn(NSDictionary* properties)
{
    id exif = properties[(__bridge NSString*)kCGImagePropertyExifDictionary];
    if (![exif isKindOfClass:[NSDictionary class]])
        return 0;
    id focal = ((NSDictionary*)exif)[(__bridge NSString*)kCGImagePropertyExifFocalLenIn35mmFilm];
    if (![focal isKindOfClass:[NSNumber class]])
        return 0;
    const double value = ((NSNumber*)focal).doubleValue;
    return value > 0 ? value : 0;
}

// Draws `image` upright (UIKit applies its orientation when drawing), at
// most kMaxPixels on its long side, and writes it as a JPEG into `folder`
// as photo-<time><suffix>.jpg. Any thread (UIGraphicsImageRenderer is
// thread-safe).
PhotoResult writeUpright(UIImage* image, NSString* folder, NSString* suffix, double focalLength35mm)
{
    if (!image)
        return failed(@"The photo could not be read.");
    const CGFloat width = image.size.width * image.scale;
    const CGFloat height = image.size.height * image.scale;
    if (width < 1 || height < 1)
        return failed(@"The photo is empty.");
    const CGFloat longSide = width > height ? width : height;
    const CGFloat factor = longSide > kMaxPixels ? kMaxPixels / longSide : 1;
    const CGSize target = CGSizeMake(std::max<CGFloat>(1, std::floor(width * factor)),
                                     std::max<CGFloat>(1, std::floor(height * factor)));

    UIGraphicsImageRendererFormat* format = [UIGraphicsImageRendererFormat preferredFormat];
    format.scale = 1;
    format.opaque = YES;
    format.preferredRange = UIGraphicsImageRendererFormatRangeStandard;
    UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithSize:target format:format];
    NSData* jpeg = [renderer JPEGDataWithCompressionQuality:0.9
                                                    actions:^(UIGraphicsImageRendererContext* context) {
                                                        (void)context;
                                                        [image drawInRect:CGRectMake(0, 0, target.width, target.height)];
                                                    }];
    if (jpeg.length == 0)
        return failed(@"The photo could not be converted to JPEG.");

    NSError* error = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:folder
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:&error])
        return failed([NSString stringWithFormat:@"The folder for photos could not be made (%@).",
                                                 error.localizedDescription]);
    const QString name = QStringLiteral("photo-%1%2.jpg")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                                  qtText(suffix));
    NSString* path = [folder stringByAppendingPathComponent:name.toNSString()];
    if (![jpeg writeToFile:path options:NSDataWritingAtomic error:&error])
        return failed([NSString stringWithFormat:@"The photo could not be saved (%@).", error.localizedDescription]);
    qInfo("Measure: photo written: %s (%.0f x %.0f px from %.0f x %.0f, %lu bytes, 35 mm focal length %.0f)",
          qPrintable(qtText(path)), static_cast<double>(target.width), static_cast<double>(target.height),
          static_cast<double>(width), static_cast<double>(height), static_cast<unsigned long>(jpeg.length),
          focalLength35mm);
    PhotoResult result;
    result.outcome = PhotoResult::Outcome::Picked;
    result.file = qtText(path);
    result.focalLength35mm = focalLength35mm;
    return result;
}

// A photo file (HEIC, JPEG, ...) read with ImageIO: its 35 mm focal length
// from the original's properties, and the image already turned upright and
// at most kMaxPixels on its long side (without decoding it at full size
// into a UIImage first). Nil when ImageIO cannot read it.
UIImage* uprightImageFromFile(NSURL* url, double* focalLength35mm)
{
    *focalLength35mm = 0;
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    if (!source)
        return nil;
    NSDictionary* properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, NULL));
    *focalLength35mm = focalLengthIn(properties);
    NSDictionary* options = @{
        (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
        (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform : @YES,
        (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize : @(static_cast<double>(kMaxPixels)),
    };
    CGImageRef cgImage = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
    CFRelease(source);
    if (!cgImage)
        return nil;
    UIImage* image = [UIImage imageWithCGImage:cgImage];
    CGImageRelease(cgImage);
    return image;
}

// The library's photo as a UIImage, when its file could not be had (no
// focal length then). `done` runs on a background queue.
void loadPhotoAsImage(NSItemProvider* provider, NSString* folder, NSString* suffix, const PhotoDone& done)
{
    if (![provider canLoadObjectOfClass:[UIImage class]]) {
        done(failed(@"The chosen item is not a photo FoldForm can read."));
        return;
    }
    const PhotoDone finish = done;
    [provider loadObjectOfClass:[UIImage class]
              completionHandler:^(id<NSItemProviderReading> object, NSError* error) {
                  if ([(id)object isKindOfClass:[UIImage class]]) {
                      PhotoResult result;
                      @autoreleasepool {
                          result = writeUpright((UIImage*)object, folder, suffix, 0);
                      }
                      finish(result);
                      return;
                  }
                  NSString* reason = error.localizedDescription;
                  finish(failed(reason ? [NSString stringWithFormat:@"The photo could not be read (%@).", reason]
                                       : @"The photo could not be read."));
              }];
}

// One photo from the library's picker: its file first (ImageIO reads the
// focal length, turns and shrinks it), a UIImage when the file cannot be
// had. `done` runs once, on a background queue.
void loadPhoto(NSItemProvider* provider, NSString* folder, NSString* suffix, const PhotoDone& done)
{
    const PhotoDone finish = done;
    [provider loadFileRepresentationForTypeIdentifier:@"public.image"
                                    completionHandler:^(NSURL* url, NSError* error) {
                                        // On a background queue; the file
                                        // is removed when this returns.
                                        double focal = 0;
                                        UIImage* image = nil;
                                        PhotoResult result;
                                        @autoreleasepool {
                                            image = url ? uprightImageFromFile(url, &focal) : nil;
                                            if (image)
                                                result = writeUpright(image, folder, suffix, focal);
                                        }
                                        if (image) {
                                            finish(result);
                                            return;
                                        }
                                        qInfo("Measure: the photo's file could not be read (%s); trying it as an image",
                                              error ? qPrintable(qtText(error.localizedDescription)) : "no file");
                                        loadPhotoAsImage(provider, folder, suffix, finish);
                                    }];
}

const char* outcomeName(PhotoResult::Outcome outcome)
{
    switch (outcome) {
    case PhotoResult::Outcome::Picked:
        return "picked";
    case PhotoResult::Outcome::Cancelled:
        return "cancelled";
    case PhotoResult::Outcome::Failed:
        return "failed";
    }
    return "?";
}

} // namespace

// The pickers' delegate, for one photo (`finishedOne`) or several from the
// library (`finishedMany`). Kept alive (activePickers) from presenting the
// picker until it finished, since a picker's delegate property is weak.
@interface OSMeasurePhotoPicker : NSObject <UIImagePickerControllerDelegate, UINavigationControllerDelegate,
                                            PHPickerViewControllerDelegate>
- (instancetype)initWithFolder:(NSString*)folder;
- (void)setFinishedOne:(const std::function<void(const PhotoResult&)>&)finished;
- (void)setFinishedMany:(const std::function<void(const std::vector<PhotoResult>&)>&)finished;
- (void)finishOne:(const PhotoResult&)result;
- (void)addResult:(const PhotoResult&)result;
- (void)finishMany;
- (void)loadFrom:(NSArray<NSItemProvider*>*)providers index:(NSUInteger)index;
@end

namespace {

NSMutableSet<OSMeasurePhotoPicker*>* activePickers()
{
    static NSMutableSet<OSMeasurePhotoPicker*>* pickers = [[NSMutableSet alloc] init];
    return pickers;
}

} // namespace

@implementation OSMeasurePhotoPicker {
    NSString* _folder;
    std::function<void(const PhotoResult&)> _finishedOne;
    std::function<void(const std::vector<PhotoResult>&)> _finishedMany;
    std::vector<PhotoResult> _results; // several: filled one after another, off the main thread
    BOOL _done;
}

- (instancetype)initWithFolder:(NSString*)folder
{
    self = [super init];
    if (self) {
        _folder = [folder copy];
        _done = NO;
    }
    return self;
}

- (void)setFinishedOne:(const std::function<void(const PhotoResult&)>&)finished
{
    _finishedOne = finished;
}

- (void)setFinishedMany:(const std::function<void(const std::vector<PhotoResult>&)>&)finished
{
    _finishedMany = finished;
}

// Any thread; the callback runs once, later, on the main thread. With
// several photos asked for, a failure before any was chosen is that one
// result.
- (void)finishOne:(const PhotoResult&)result
{
    const PhotoResult copy = result;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_done)
            return;
        self->_done = YES;
        const std::function<void(const PhotoResult&)> one = self->_finishedOne;
        const std::function<void(const std::vector<PhotoResult>&)> many = self->_finishedMany;
        self->_finishedOne = nullptr;
        self->_finishedMany = nullptr;
        [activePickers() removeObject:self];
        qInfo("Measure: photo picker finished: %s%s%s", outcomeName(copy.outcome), copy.error.isEmpty() ? "" : ": ",
              qPrintable(copy.error));
        if (one) {
            one(copy);
        } else if (many) {
            std::vector<PhotoResult> results;
            if (copy.outcome != PhotoResult::Outcome::Cancelled)
                results.push_back(copy);
            many(results);
        }
    });
}

- (void)addResult:(const PhotoResult&)result
{
    _results.push_back(result);
}

// After the last of several photos (the loads run one after another, so
// `_results` is complete when this runs).
- (void)finishMany
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_done)
            return;
        self->_done = YES;
        const std::vector<PhotoResult> results = self->_results;
        const std::function<void(const std::vector<PhotoResult>&)> many = self->_finishedMany;
        self->_finishedOne = nullptr;
        self->_finishedMany = nullptr;
        [activePickers() removeObject:self];
        int picked = 0;
        for (const PhotoResult& r : results)
            picked += r.outcome == PhotoResult::Outcome::Picked ? 1 : 0;
        qInfo("Measure: photo picker finished with %d of %d photo(s) written", picked, static_cast<int>(results.size()));
        if (many)
            many(results);
    });
}

// Several photos, one after another (each a full-size decode: not at once).
- (void)loadFrom:(NSArray<NSItemProvider*>*)providers index:(NSUInteger)index
{
    if (index >= providers.count) {
        [self finishMany];
        return;
    }
    OSMeasurePhotoPicker* picker = self;
    NSString* suffix = [NSString stringWithFormat:@"-%lu", static_cast<unsigned long>(index + 1)];
    loadPhoto(providers[index], _folder, suffix, [picker, providers, index](const PhotoResult& result) {
        [picker addResult:result];
        [picker loadFrom:providers index:index + 1];
    });
}

// ---- UIImagePickerControllerDelegate (the camera; main thread)

- (void)imagePickerController:(UIImagePickerController*)picker
    didFinishPickingMediaWithInfo:(NSDictionary<UIImagePickerControllerInfoKey, id>*)info
{
    UIImage* image = info[UIImagePickerControllerOriginalImage];
    id metadata = info[UIImagePickerControllerMediaMetadata];
    const double focal = [metadata isKindOfClass:[NSDictionary class]] ? focalLengthIn((NSDictionary*)metadata) : 0;
    qInfo("Measure: camera returned %s (35 mm focal length %.0f)", image ? "a photo" : "no photo", focal);
    [picker dismissViewControllerAnimated:YES completion:nil];
    if (!image) {
        [self finishOne:failed(@"The camera gave no photo.")];
        return;
    }
    NSString* folder = _folder;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        PhotoResult result;
        @autoreleasepool {
            result = writeUpright(image, folder, @"", focal);
        }
        [self finishOne:result];
    });
}

- (void)imagePickerControllerDidCancel:(UIImagePickerController*)picker
{
    [picker dismissViewControllerAnimated:YES completion:nil];
    PhotoResult result;
    result.outcome = PhotoResult::Outcome::Cancelled;
    [self finishOne:result];
}

// ---- PHPickerViewControllerDelegate (the photo library; main thread;
// also called with no results when cancelled)

- (void)picker:(PHPickerViewController*)picker didFinishPicking:(NSArray<PHPickerResult*>*)results
{
    qInfo("Measure: photo library picker returned %lu result(s)", static_cast<unsigned long>(results.count));
    [picker dismissViewControllerAnimated:YES completion:nil];
    if (results.count == 0) {
        PhotoResult cancelled;
        cancelled.outcome = PhotoResult::Outcome::Cancelled;
        [self finishOne:cancelled];
        return;
    }
    if (_finishedMany) {
        NSMutableArray<NSItemProvider*>* providers = [[NSMutableArray alloc] init];
        for (PHPickerResult* result in results)
            [providers addObject:result.itemProvider];
        [self loadFrom:providers index:0];
        return;
    }
    OSMeasurePhotoPicker* owner = self;
    loadPhoto(results.firstObject.itemProvider, _folder, @"",
              [owner](const PhotoResult& result) { [owner finishOne:result]; });
}

@end

namespace os::ui::measureios {

namespace {

void presentCamera(UIView* view, OSMeasurePhotoPicker* delegate)
{
    UIViewController* presenter = presenterFor(view);
    if (!presenter) {
        [delegate finishOne:failed(@"There is no window to show the camera in.")];
        return;
    }
    UIImagePickerController* picker = [[UIImagePickerController alloc] init];
    picker.sourceType = UIImagePickerControllerSourceTypeCamera;
    picker.allowsEditing = NO;
    picker.delegate = delegate;
    picker.modalPresentationStyle = UIModalPresentationFullScreen;
    qInfo("Measure: showing the camera");
    [presenter presentViewController:picker animated:YES completion:nil];
}

void presentLibrary(UIView* view, const QRectF& anchor, NSInteger limit, OSMeasurePhotoPicker* delegate)
{
    UIViewController* presenter = presenterFor(view);
    if (!presenter) {
        [delegate finishOne:failed(@"There is no window to show the photo library in.")];
        return;
    }
    PHPickerConfiguration* configuration = [[PHPickerConfiguration alloc] init];
    configuration.filter = [PHPickerFilter imagesFilter];
    configuration.selectionLimit = limit;
    PHPickerViewController* picker = [[PHPickerViewController alloc] initWithConfiguration:configuration];
    picker.delegate = delegate;
    // A sheet on every device; should UIKit make it a popover (an iPad), it
    // points at the control that asked.
    if (UIPopoverPresentationController* popover = picker.popoverPresentationController) {
        popover.sourceView = view;
        if (!anchor.isEmpty()) {
            popover.sourceRect = anchor.toCGRect();
            popover.permittedArrowDirections = UIPopoverArrowDirectionAny;
        } else {
            popover.sourceRect = CGRectMake(CGRectGetMidX(view.bounds), CGRectGetMidY(view.bounds), 0, 0);
            popover.permittedArrowDirections = 0;
        }
    }
    qInfo("Measure: showing the photo library picker (up to %ld photo(s))", static_cast<long>(limit));
    [presenter presentViewController:picker animated:YES completion:nil];
}

void presentCameraAfterAsking(UIView* view, OSMeasurePhotoPicker* delegate)
{
    if (![UIImagePickerController isSourceTypeAvailable:UIImagePickerControllerSourceTypeCamera]) {
        [delegate finishOne:failed(@"This device has no camera to take a photo with.")];
        return;
    }
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    qInfo("Measure: camera permission status %d", static_cast<int>(status));
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
        [delegate finishOne:failed(cameraDeniedMessage().toNSString())];
        return;
    }
    if (status == AVAuthorizationStatusNotDetermined) {
        // iOS asks once (NSCameraUsageDescription); the answer comes on
        // any queue.
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                     dispatch_async(dispatch_get_main_queue(), ^{
                                         qInfo("Measure: camera permission %s", granted ? "granted" : "refused");
                                         if (granted)
                                             presentCamera(view, delegate);
                                         else
                                             [delegate finishOne:failed(cameraDeniedMessage().toNSString())];
                                     });
                                 }];
        return;
    }
    presentCamera(view, delegate);
}

void present(QWindow* window, PhotoOrigin origin, const QString& folder, const QRectF& anchor,
             const std::function<void(const PhotoResult&)>& finished)
{
    OSMeasurePhotoPicker* delegate = [[OSMeasurePhotoPicker alloc] initWithFolder:folder.toNSString()];
    [delegate setFinishedOne:finished];
    [activePickers() addObject:delegate];
    UIView* view = hostView(window);
    if (!view) {
        [delegate finishOne:failed(@"There is no window to show the picker in.")];
        return;
    }
    if (origin == PhotoOrigin::Library)
        presentLibrary(view, anchor, 1, delegate);
    else
        presentCameraAfterAsking(view, delegate);
}

void presentMany(QWindow* window, int maxCount, const QString& folder, const QRectF& anchor,
                 const std::function<void(const std::vector<PhotoResult>&)>& finished)
{
    OSMeasurePhotoPicker* delegate = [[OSMeasurePhotoPicker alloc] initWithFolder:folder.toNSString()];
    [delegate setFinishedMany:finished];
    [activePickers() addObject:delegate];
    UIView* view = hostView(window);
    if (!view) {
        [delegate finishOne:failed(@"There is no window to show the photo library in.")];
        return;
    }
    // PHPicker: 0 would mean no limit; at least one.
    presentLibrary(view, anchor, std::max(1, maxCount), delegate);
}

} // namespace

void pickPhoto(QWindow* window, PhotoOrigin origin, const QString& folder, const QRectF& anchor,
               std::function<void(const PhotoResult&)> finished)
{
    // UIKit only on the main thread (Qt's GUI thread on iOS; to be sure).
    if ([NSThread isMainThread]) {
        present(window, origin, folder, anchor, finished);
        return;
    }
    const QString folderCopy = folder;
    const QRectF anchorCopy = anchor;
    const std::function<void(const PhotoResult&)> finishedCopy = std::move(finished);
    dispatch_async(dispatch_get_main_queue(), ^{
        present(window, origin, folderCopy, anchorCopy, finishedCopy);
    });
}

void pickPhotos(QWindow* window, int maxCount, const QString& folder, const QRectF& anchor,
                std::function<void(const std::vector<PhotoResult>&)> finished)
{
    if ([NSThread isMainThread]) {
        presentMany(window, maxCount, folder, anchor, finished);
        return;
    }
    const QString folderCopy = folder;
    const QRectF anchorCopy = anchor;
    const std::function<void(const std::vector<PhotoResult>&)> finishedCopy = std::move(finished);
    dispatch_async(dispatch_get_main_queue(), ^{
        presentMany(window, maxCount, folderCopy, anchorCopy, finishedCopy);
    });
}

} // namespace os::ui::measureios
