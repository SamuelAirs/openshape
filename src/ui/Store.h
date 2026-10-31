// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>

#include <functional>
#include <memory>

namespace os::ui {

// The App Store's in-app purchase "Unlimited projects" (iPhone / iPad). The
// free app keeps one project in OpenShape's folder; the purchase lifts the
// limit (ui/Purchase.h). The AppController is given a Store
// (AppController::setStore): the platform's (platformStore, iOS only), a
// stand-in in the acceptance run, none elsewhere (no limit, no purchase UI).
// Qt Core only.

// What the store reports, always on the GUI thread.
struct StoreEvent {
    enum class Kind {
        ProductLoaded,      // `price`: the product's price, formatted for the user's store
        ProductUnavailable, // `message`: why there is none (offline, not set up, purchases turned off)
        Purchasing,         // a payment is being processed
        Deferred,           // waits for approval (Ask to Buy); it may complete in a later run
        Purchased,          // bought: also one that arrives unrequested (an interrupted
                            // purchase, Ask to Buy approved later, bought on the App Store page)
        Restored,           // an earlier purchase brought back
        Failed,             // `message`: why the purchase did not go through
        Cancelled,          // the user closed Apple's purchase sheet
        RestoreFinished,    // Restore purchase has ended (any Restored came before it)
        RestoreFailed,      // `message`: why
    };
    Kind kind = Kind::Failed;
    QString productId;
    QString price;
    QString message;
};

using StoreListener = std::function<void(const StoreEvent& event)>;

class Store {
public:
    virtual ~Store() = default;
    // Where events go, from now on (the platform store starts watching
    // for transactions with the first listener, so those left from an
    // earlier run are not lost).
    virtual void setListener(StoreListener listener) = 0;
    // Asks for the product and its price (ProductLoaded / ProductUnavailable).
    virtual void requestProduct() = 0;
    // Buys the product (needs a ProductLoaded first).
    virtual void buy() = 0;
    // Restore purchase: Restored for each earlier purchase, then RestoreFinished.
    virtual void restore() = 0;
};

// The product's identifier: "<bundle id>.unlimited", from the
// OPENSHAPE_BUNDLE_ID CMake cache variable (io.github.samuelairs.openshape.unlimited;
// core/Version.h.in).
QString unlockProductId();

// iOS: StoreKit (src/ui/ios/StoreKitStore.mm). Elsewhere null: no store.
std::shared_ptr<Store> platformStore(const QString& productId);

} // namespace os::ui
