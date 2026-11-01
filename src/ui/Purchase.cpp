// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/Purchase.h"

#include "core/Version.h"
#include "ui/IncomingFiles.h"

#include <QtCore/QDir>
#include <QtCore/QSettings>

#include <utility>

namespace os::ui {

namespace {

const char* const kUnlockedKey = "store/unlimitedProjects";

QString unavailableText()
{
    return QStringLiteral("The App Store can't be reached right now. Check the connection and try again later.");
}

} // namespace

QString unlockProductId()
{
    // "<bundle id>.unlimited", from OPENSHAPE_BUNDLE_ID (core/Version.h.in).
    return QString::fromLatin1(kUnlockProductId);
}

namespace {

// Adds the projects in `dir` and the folders below it. A leading dot hides
// a file or folder on iOS (not on Windows): skipped everywhere, which also
// leaves out the Files app's Recently Deleted (.Trash). Links are not
// followed, so a folder linked into itself cannot loop.
void collectProjects(const QDir& dir, bool top, QStringList& projects)
{
    // Name filters ignore case unless asked.
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.openshape")}, QDir::Files, QDir::Name))
        if (!info.fileName().startsWith(QLatin1Char('.')))
            projects << info.absoluteFilePath();
    const QFileInfoList folders = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo& info : folders) {
        const QString name = info.fileName();
        if (name.startsWith(QLatin1Char('.')))
            continue;
        // Exports holds STL / 3MF / STEP files, and iOS's Inbox holds copies
        // of mailed files that are not taken in yet: neither is a project.
        if (top && (name.compare(QLatin1String("Exports"), Qt::CaseInsensitive) == 0
                    || name.compare(QLatin1String("Inbox"), Qt::CaseInsensitive) == 0))
            continue;
        collectProjects(QDir(info.absoluteFilePath()), false, projects);
    }
}

} // namespace

QStringList appFolderProjects(const QString& folder)
{
    if (folder.isEmpty())
        return {};
    // The whole folder, not only its top: a project moved into a folder of
    // its own in the Files app still counts (TD-155; it opens in place).
    QStringList projects;
    collectProjects(QDir(folder), true, projects);
    return projects;
}

bool limitAllowsProject(const ProjectLimit& limit, const QString& replacing)
{
    if (!limit.store || limit.unlocked || limit.appFolder.isEmpty())
        return true;
    int others = 0;
    for (const QString& project : appFolderProjects(limit.appFolder))
        if (replacing.isEmpty() || !isInsideFolder(project, replacing)) // (a file: the same path)
            ++others;
    return others < kFreeProjectCount;
}

bool loadProjectsUnlocked(QSettings& settings)
{
    return settings.value(QLatin1String(kUnlockedKey)).toBool();
}

void saveProjectsUnlocked(QSettings& settings, bool unlocked)
{
    if (unlocked)
        settings.setValue(QLatin1String(kUnlockedKey), true);
    else
        settings.remove(QLatin1String(kUnlockedKey));
}

QString purchaseStatusName(PurchaseStatus status)
{
    switch (status) {
    case PurchaseStatus::Loading: return QStringLiteral("loading");
    case PurchaseStatus::Ready: return QStringLiteral("ready");
    case PurchaseStatus::Buying: return QStringLiteral("buying");
    case PurchaseStatus::Waiting: return QStringLiteral("waiting");
    case PurchaseStatus::Restoring: return QStringLiteral("restoring");
    case PurchaseStatus::Failed: return QStringLiteral("failed");
    case PurchaseStatus::Unavailable: return QStringLiteral("unavailable");
    }
    return QStringLiteral("unavailable");
}

const char* storeEventName(StoreEvent::Kind kind)
{
    switch (kind) {
    case StoreEvent::Kind::ProductLoaded: return "price received";
    case StoreEvent::Kind::ProductUnavailable: return "product unavailable";
    case StoreEvent::Kind::Purchasing: return "purchasing";
    case StoreEvent::Kind::Deferred: return "waiting for approval";
    case StoreEvent::Kind::Purchased: return "purchased";
    case StoreEvent::Kind::Restored: return "restored";
    case StoreEvent::Kind::Failed: return "purchase failed";
    case StoreEvent::Kind::Cancelled: return "purchase cancelled";
    case StoreEvent::Kind::RestoreFinished: return "restore finished";
    case StoreEvent::Kind::RestoreFailed: return "restore failed";
    }
    return "unknown";
}

PurchaseFlow::PurchaseFlow(QString productId, bool unlocked)
    : productId_(std::move(productId)), unlocked_(unlocked)
{
}

bool PurchaseFlow::canBuy() const
{
    return !unlocked_ && hasProduct() && status_ != PurchaseStatus::Loading && status_ != PurchaseStatus::Buying
        && status_ != PurchaseStatus::Restoring;
}

bool PurchaseFlow::opening()
{
    if (unlocked_ || status_ == PurchaseStatus::Buying || status_ == PurchaseStatus::Restoring || status_ == PurchaseStatus::Waiting)
        return false;
    message_.clear(); // a failure from the last time the sheet was open is over
    if (hasProduct()) {
        status_ = PurchaseStatus::Ready;
        return false;
    }
    status_ = PurchaseStatus::Loading;
    return true;
}

bool PurchaseFlow::startBuy()
{
    if (!canBuy())
        return false;
    status_ = PurchaseStatus::Buying;
    message_.clear();
    return true;
}

bool PurchaseFlow::startRestore()
{
    if (status_ == PurchaseStatus::Buying || status_ == PurchaseStatus::Restoring)
        return false;
    status_ = PurchaseStatus::Restoring;
    message_.clear();
    return true;
}

void PurchaseFlow::settle()
{
    status_ = hasProduct() ? PurchaseStatus::Ready : PurchaseStatus::Unavailable;
    if (status_ == PurchaseStatus::Unavailable && message_.isEmpty())
        message_ = unavailableText();
}

bool PurchaseFlow::apply(const StoreEvent& event)
{
    using Kind = StoreEvent::Kind;
    // Transactions of another product (there is none today) change nothing.
    const bool ours = event.productId.isEmpty() || event.productId == productId_;
    const bool busy = status_ == PurchaseStatus::Buying || status_ == PurchaseStatus::Restoring || status_ == PurchaseStatus::Waiting;
    switch (event.kind) {
    case Kind::ProductLoaded:
        if (!ours || event.price.isEmpty())
            return false;
        price_ = event.price;
        if (status_ == PurchaseStatus::Loading || status_ == PurchaseStatus::Unavailable) {
            status_ = PurchaseStatus::Ready;
            message_.clear();
        }
        return false;
    case Kind::ProductUnavailable:
        if (!ours)
            return false;
        price_.clear();
        if (!busy) {
            status_ = PurchaseStatus::Unavailable;
            message_ = event.message.isEmpty() ? unavailableText() : event.message;
        }
        return false;
    case Kind::Purchasing:
        if (!ours)
            return false;
        status_ = PurchaseStatus::Buying;
        message_.clear();
        return false;
    case Kind::Deferred:
        if (!ours)
            return false;
        status_ = PurchaseStatus::Waiting;
        message_.clear();
        return false;
    case Kind::Purchased:
    case Kind::Restored: {
        if (!ours)
            return false;
        const bool was = unlocked_;
        unlocked_ = true;
        message_.clear();
        settle();
        return !was;
    }
    case Kind::Failed:
        if (!ours)
            return false;
        status_ = PurchaseStatus::Failed;
        message_ = event.message.isEmpty() ? QStringLiteral("The purchase did not go through.")
                                           : QStringLiteral("The purchase did not go through: %1").arg(event.message);
        return false;
    case Kind::Cancelled:
        if (!ours)
            return false;
        message_.clear();
        settle();
        return false;
    case Kind::RestoreFinished:
        if (status_ != PurchaseStatus::Restoring)
            return false;
        message_.clear();
        settle();
        if (!unlocked_)
            message_ = QStringLiteral("No earlier purchase of unlimited projects was found for this Apple Account.");
        return false;
    case Kind::RestoreFailed:
        status_ = PurchaseStatus::Failed;
        message_ = event.message.isEmpty() ? QStringLiteral("The purchase could not be restored.")
                                           : QStringLiteral("The purchase could not be restored: %1").arg(event.message);
        return false;
    }
    return false;
}

} // namespace os::ui
