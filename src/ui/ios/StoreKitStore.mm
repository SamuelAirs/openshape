// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The App Store purchase "Unlimited projects" (ui/Store.h) with StoreKit's
// original API: SKProductsRequest for the price, SKPaymentQueue for the
// purchase and Restore purchase, with a transaction observer added when the
// AppController sets its listener at start, so that purchases left
// unfinished by an earlier run (an interrupted purchase, Ask to Buy approved
// later) and purchases started on the App Store page arrive and unlock.
// A transaction is finished only after its event reached the listener,
// which remembers the unlock; otherwise StoreKit hands it over again at the
// next start. Everything reaches the listener on the main thread (Qt's GUI
// thread on iOS). Compiled for iOS only, with ARC (src/ui/CMakeLists.txt).
//
// StoreKit 1 is deprecated since iOS 18 in favour of StoreKit 2, which is
// Swift only; the iOS 27.1 SDK still has it. Its deprecation warnings are
// silenced in this file only (docs/TECHNICAL_DEBT.md: migrate to StoreKit 2).

#include "ui/Store.h"

#include <QtCore/QString>

#include <utility>

#import <Foundation/Foundation.h>
#import <StoreKit/StoreKit.h>

#if !__has_feature(objc_arc)
#error "StoreKitStore.mm is written for ARC (-fobjc-arc, see src/ui/CMakeLists.txt)"
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-implementations"

namespace os::ui {
class IosStore;
}

// StoreKit's delegate and transaction observer. Owned (strongly) by the
// IosStore; the products request it starts is kept until it ends (its
// delegate property is weak).
@interface OSStoreKitBridge : NSObject <SKPaymentTransactionObserver, SKProductsRequestDelegate>
- (instancetype)initWithProductId:(NSString*)productId owner:(os::ui::IosStore*)owner;
- (void)startObserving;
- (void)detach;
- (void)requestProduct;
- (void)buy;
- (void)restore;
@end

namespace os::ui {

class IosStore final : public Store {
public:
    explicit IosStore(const QString& productId)
    {
        bridge_ = [[OSStoreKitBridge alloc] initWithProductId:productId.toNSString() owner:this];
    }
    ~IosStore() override { [bridge_ detach]; }

    void setListener(StoreListener listener) override
    {
        listener_ = std::move(listener);
        if (listener_)
            [bridge_ startObserving];
    }
    void requestProduct() override { [bridge_ requestProduct]; }
    void buy() override { [bridge_ buy]; }
    void restore() override { [bridge_ restore]; }

    // On the main thread. False when nobody listens (a transaction then
    // stays unfinished, for the next start).
    bool deliver(const StoreEvent& event)
    {
        if (!listener_)
            return false;
        // A copy: the listener may replace this store while it runs.
        const StoreListener listener = listener_;
        listener(event);
        return true;
    }

private:
    OSStoreKitBridge* bridge_ = nil;
    StoreListener listener_;
};

std::shared_ptr<Store> platformStore(const QString& productId)
{
    return std::make_shared<IosStore>(productId);
}

} // namespace os::ui

using os::ui::StoreEvent;

@implementation OSStoreKitBridge {
    NSString* _productId;
    os::ui::IosStore* _owner; // null once detached
    SKProductsRequest* _request;
    SKProduct* _product;
    BOOL _observing;
}

- (instancetype)initWithProductId:(NSString*)productId owner:(os::ui::IosStore*)owner
{
    self = [super init];
    if (self) {
        _productId = [productId copy];
        _owner = owner;
    }
    return self;
}

- (void)startObserving
{
    if (_observing)
        return;
    _observing = YES;
    [[SKPaymentQueue defaultQueue] addTransactionObserver:self];
}

- (void)detach
{
    _owner = nullptr;
    if (_observing) {
        [[SKPaymentQueue defaultQueue] removeTransactionObserver:self];
        _observing = NO;
    }
    if (_request) {
        _request.delegate = nil;
        [_request cancel];
        _request = nil;
    }
}

// On the main thread.
- (BOOL)deliver:(StoreEvent::Kind)kind price:(NSString*)price message:(NSString*)message
{
    if (!_owner)
        return NO;
    StoreEvent event;
    event.kind = kind;
    event.productId = QString::fromNSString(_productId);
    if (price)
        event.price = QString::fromNSString(price);
    if (message)
        event.message = QString::fromNSString(message);
    return _owner->deliver(event) ? YES : NO;
}

// Later, on the main thread (also when called there: never from inside a
// call the listener made).
- (void)post:(StoreEvent::Kind)kind message:(NSString*)message
{
    NSString* text = [message copy];
    dispatch_async(dispatch_get_main_queue(), ^{
        [self deliver:kind price:nil message:text];
    });
}

- (void)requestProduct
{
    if (![SKPaymentQueue canMakePayments]) {
        [self post:StoreEvent::Kind::ProductUnavailable
            message:@"Purchases are turned off on this device (Screen Time, Content & Privacy Restrictions)."];
        return;
    }
    if (_request)
        return; // one is on its way
    _request = [[SKProductsRequest alloc] initWithProductIdentifiers:[NSSet setWithObject:_productId]];
    _request.delegate = self;
    [_request start];
}

- (void)buy
{
    if (!_product) {
        [self post:StoreEvent::Kind::Failed message:@"the price has not arrived from the App Store yet. Try again in a moment."];
        [self requestProduct];
        return;
    }
    [self startObserving];
    [[SKPaymentQueue defaultQueue] addPayment:[SKPayment paymentWithProduct:_product]];
}

- (void)restore
{
    [self startObserving];
    [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
}

// ---- SKProductsRequestDelegate (may be called on any thread)

- (void)productsRequest:(SKProductsRequest*)request didReceiveResponse:(SKProductsResponse*)response
{
    SKProduct* found = nil;
    for (SKProduct* product in response.products)
        if ([product.productIdentifier isEqualToString:_productId])
            found = product;
    NSString* price = nil;
    if (found) {
        NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
        formatter.numberStyle = NSNumberFormatterCurrencyStyle;
        formatter.locale = found.priceLocale;
        price = [formatter stringFromNumber:found.price];
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_request == request)
            self->_request = nil;
        if (found && price.length > 0) {
            self->_product = found;
            [self deliver:StoreEvent::Kind::ProductLoaded price:price message:nil];
        } else {
            [self deliver:StoreEvent::Kind::ProductUnavailable
                    price:nil
                  message:@"Unlimited projects is not available from the App Store right now. Try again later."];
        }
    });
}

- (void)request:(SKRequest*)request didFailWithError:(NSError*)error
{
    NSString* reason = [error.localizedDescription copy];
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_request == request)
            self->_request = nil;
        NSString* text = reason.length > 0
            ? [NSString stringWithFormat:@"The App Store can't be reached right now (%@). Check the connection and try again later.", reason]
            : @"The App Store can't be reached right now. Check the connection and try again later.";
        [self deliver:StoreEvent::Kind::ProductUnavailable price:nil message:text];
    });
}

- (void)requestDidFinish:(SKRequest*)request
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_request == request)
            self->_request = nil;
    });
}

// ---- SKPaymentTransactionObserver (called on the main thread; hopped
// there anyway, in order, so the listener never runs inside StoreKit)

- (void)paymentQueue:(SKPaymentQueue*)queue updatedTransactions:(NSArray<SKPaymentTransaction*>*)transactions
{
    (void)queue;
    for (SKPaymentTransaction* transaction in transactions) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self handleTransaction:transaction];
        });
    }
}

- (void)handleTransaction:(SKPaymentTransaction*)transaction
{
    SKPaymentQueue* queue = [SKPaymentQueue defaultQueue];
    const BOOL ours = [transaction.payment.productIdentifier isEqualToString:_productId];
    const SKPaymentTransactionState state = transaction.transactionState;
    if (state == SKPaymentTransactionStatePurchasing) {
        if (ours)
            [self deliver:StoreEvent::Kind::Purchasing price:nil message:nil];
        return; // in progress: not finished
    }
    if (state == SKPaymentTransactionStateDeferred) {
        if (ours)
            [self deliver:StoreEvent::Kind::Deferred price:nil message:nil];
        return; // it comes back as purchased or failed, maybe in a later run
    }
    if (state == SKPaymentTransactionStatePurchased || state == SKPaymentTransactionStateRestored) {
        if (!ours) {
            [queue finishTransaction:transaction]; // nothing of OpenShape's to unlock
            return;
        }
        const StoreEvent::Kind kind =
            state == SKPaymentTransactionStatePurchased ? StoreEvent::Kind::Purchased : StoreEvent::Kind::Restored;
        // Finished only once the unlock is remembered (the listener did that).
        if ([self deliver:kind price:nil message:nil])
            [queue finishTransaction:transaction];
        return;
    }
    if (state == SKPaymentTransactionStateFailed) {
        if (ours) {
            NSError* error = transaction.error;
            if ([error.domain isEqualToString:SKErrorDomain] && error.code == SKErrorPaymentCancelled)
                [self deliver:StoreEvent::Kind::Cancelled price:nil message:nil];
            else
                [self deliver:StoreEvent::Kind::Failed price:nil message:error.localizedDescription];
        }
        [queue finishTransaction:transaction];
        return;
    }
}

- (void)paymentQueueRestoreCompletedTransactionsFinished:(SKPaymentQueue*)queue
{
    (void)queue;
    [self post:StoreEvent::Kind::RestoreFinished message:nil];
}

- (void)paymentQueue:(SKPaymentQueue*)queue restoreCompletedTransactionsFailedWithError:(NSError*)error
{
    (void)queue;
    if ([error.domain isEqualToString:SKErrorDomain] && error.code == SKErrorPaymentCancelled)
        [self post:StoreEvent::Kind::Cancelled message:nil]; // the sign-in was cancelled
    else
        [self post:StoreEvent::Kind::RestoreFailed message:error.localizedDescription];
}

// A purchase started on the app's App Store page: let it go ahead. It then
// arrives in updatedTransactions like any other and unlocks.
- (BOOL)paymentQueue:(SKPaymentQueue*)queue shouldAddStorePayment:(SKPayment*)payment forProduct:(SKProduct*)product
{
    (void)queue;
    (void)payment;
    (void)product;
    return YES;
}

@end

#pragma clang diagnostic pop
