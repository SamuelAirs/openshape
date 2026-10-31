// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "ui/Store.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

class QSettings;

namespace os::ui {

// The free App Store app keeps this many projects in OpenShape's folder;
// "Unlimited projects" (ui/Store.h) lifts the limit. Qt Core only
// (tests/test_purchase.cpp).
inline constexpr int kFreeProjectCount = 1;

// The projects in OpenShape's own folder (Documents on iOS): its
// *.openshape files, in any case. Not what lies in folders below it
// (Exports, the Inbox, the Files app's Recently Deleted in .Trash), nor
// hidden files. Sorted by name.
QStringList appFolderProjects(const QString& folder);

struct ProjectLimit {
    bool store = false;    // there is a store (iPhone / iPad): otherwise no limit
    bool unlocked = false; // Unlimited projects was bought or restored
    QString appFolder;     // where projects are counted (empty: no limit)
};

// Whether a project may be added: always without a store, once unlocked
// or without an app folder; otherwise only while the folder holds fewer
// than kFreeProjectCount projects besides `replacing` (a project file that
// is written over, re-saved or opened: it does not count against itself).
bool limitAllowsProject(const ProjectLimit& limit, const QString& replacing = QString());

// The unlocked flag, kept on the device (QSettings; no account, no server).
bool loadProjectsUnlocked(QSettings& settings);
void saveProjectsUnlocked(QSettings& settings, bool unlocked);

// The purchase sheet's state, driven by what the user taps and what the
// store reports.
enum class PurchaseStatus {
    Loading,     // asking the store for the price
    Ready,       // the price is there: Unlock can be tapped
    Buying,      // a purchase is being processed
    Waiting,     // waiting for approval (Ask to Buy)
    Restoring,   // Restore purchase is running
    Failed,      // the purchase or restore failed (message says why)
    Unavailable, // no product: offline, not set up, purchases turned off
};
// "loading", "ready", "buying", "waiting", "restoring", "failed", "unavailable".
QString purchaseStatusName(PurchaseStatus status);
const char* storeEventName(StoreEvent::Kind kind);

class PurchaseFlow {
public:
    explicit PurchaseFlow(QString productId = QString(), bool unlocked = false);

    const QString& productId() const { return productId_; }
    bool unlocked() const { return unlocked_; }
    PurchaseStatus status() const { return status_; }
    // The localized price ("" until the store sent it).
    const QString& price() const { return price_; }
    // Why something failed or found nothing ("" otherwise).
    const QString& message() const { return message_; }
    bool hasProduct() const { return !price_.isEmpty(); }
    // Unlock can be tapped: a price, not unlocked, nothing running.
    bool canBuy() const;

    // The sheet opens. True when the price must be asked for (none yet).
    bool opening();
    // Unlock was tapped: false (nothing to do) unless canBuy().
    bool startBuy();
    // Restore purchase was tapped: false while a purchase or restore runs.
    bool startRestore();
    // What the store reported; true when it unlocked just now.
    bool apply(const StoreEvent& event);

private:
    // Back to rest after something ended: Ready with a price, else Unavailable.
    void settle();

    QString productId_;
    bool unlocked_ = false;
    PurchaseStatus status_ = PurchaseStatus::Loading;
    QString price_;
    QString message_;
};

} // namespace os::ui
