// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The iOS share sheet for one file (ui/Share.h): UIActivityViewController
// presented by the view controller of the app's window. On an iPad it is a
// popover pointing at the control that asked (the File button); on an
// iPhone (and in a narrow iPad window) UIKit shows it as a sheet from the
// bottom. Compiled for iOS only, with ARC (src/ui/CMakeLists.txt).

#include "ui/Share.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#if !__has_feature(objc_arc)
#error "ShareSheet.mm is written for ARC (-fobjc-arc, see src/ui/CMakeLists.txt)"
#endif

namespace os::ui {

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

// Who presents the sheet: the window's root view controller, or what it
// presents already (a sheet must be presented by the topmost one).
UIViewController* presenterFor(UIView* view)
{
    UIViewController* controller = view.window.rootViewController;
    while (controller.presentedViewController && !controller.presentedViewController.isBeingDismissed)
        controller = controller.presentedViewController;
    return controller;
}

void present(const ShareRequest& request, const ShareFinished& finished)
{
    QWindow* window = appWindow();
    // A QWindow's native id on iOS is its UIView (Qt's QUIView), whose
    // points are the window's logical coordinates.
    UIView* view = window ? (__bridge UIView*)reinterpret_cast<void*>(window->winId()) : nil;
    UIViewController* presenter = view ? presenterFor(view) : nil;
    if (!presenter) {
        finished(ShareOutcome::Failed, QStringLiteral("there is no window to show the share sheet in"));
        return;
    }

    NSURL* url = [NSURL fileURLWithPath:request.file.toNSString()];
    UIActivityViewController* sheet = [[UIActivityViewController alloc] initWithActivityItems:@[ url ]
                                                                        applicationActivities:nil];
    const ShareFinished done = finished; // copied into the block
    sheet.completionWithItemsHandler = ^(UIActivityType activityType, BOOL completed, NSArray* returnedItems,
                                         NSError* activityError) {
        (void)returnedItems;
        if (activityError)
            done(ShareOutcome::Failed, QString::fromNSString(activityError.localizedDescription));
        else if (completed)
            done(ShareOutcome::Completed, activityType ? QString::fromNSString(activityType) : QString());
        else
            done(ShareOutcome::Cancelled, QString());
    };

    // Required on an iPad (UIKit refuses a popover without a source).
    if (UIPopoverPresentationController* popover = sheet.popoverPresentationController) {
        popover.sourceView = view;
        if (!request.anchor.isEmpty()) {
            popover.sourceRect = request.anchor.toCGRect();
            popover.permittedArrowDirections = UIPopoverArrowDirectionAny;
        } else {
            popover.sourceRect = CGRectMake(CGRectGetMidX(view.bounds), CGRectGetMidY(view.bounds), 0, 0);
            popover.permittedArrowDirections = 0;
        }
    }
    [presenter presentViewController:sheet animated:YES completion:nil];
}

} // namespace

ShareHandler platformShareHandler()
{
    return [](const ShareRequest& request, ShareFinished finished) {
        // UIKit only on the main thread (Qt's GUI thread on iOS; to be sure).
        if ([NSThread isMainThread]) {
            present(request, finished);
            return;
        }
        const ShareRequest copy = request;
        dispatch_async(dispatch_get_main_queue(), ^{
            present(copy, finished);
        });
    };
}

} // namespace os::ui
