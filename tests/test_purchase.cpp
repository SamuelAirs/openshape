// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The free App Store version's one project and the purchase "Unlimited
// projects" (ui/Purchase.h, ui/Store.h): what counts as a project, when
// one more may be added, a file from another app that would add one
// (ui/IncomingFiles), and the purchase sheet's states. Qt Core only.

#include "ui/IncomingFiles.h"
#include "ui/Purchase.h"
#include "ui/Store.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include <gtest/gtest.h>

using namespace os::ui;

namespace {

void writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << path.toStdString();
    file.write(bytes);
}

StoreEvent event(StoreEvent::Kind kind, const QString& price = {}, const QString& message = {},
                 const QString& product = unlockProductId())
{
    StoreEvent e;
    e.kind = kind;
    e.productId = product;
    e.price = price;
    e.message = message;
    return e;
}

} // namespace

TEST(Purchase, ProductIdIsTheBundleIdWithUnlimited)
{
    const QString id = unlockProductId();
    EXPECT_TRUE(id.endsWith(QStringLiteral(".unlimited"))) << id.toStdString();
    EXPECT_GT(id.size(), int(sizeof(".unlimited")));
}

TEST(Purchase, OnlyProjectFilesInTheFolderItselfCount)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString folder = dir.path();
    EXPECT_TRUE(appFolderProjects(folder).isEmpty());
    writeFile(folder + "/Bracket.openshape", "p");
    writeFile(folder + "/Upper.OPENSHAPE", "p");                  // any case
    writeFile(folder + "/Exports/Bracket.openshape", "p");        // exports are not projects
    writeFile(folder + "/Exports/Bracket.stl", "s");
    writeFile(folder + "/Inbox/Mailed.openshape", "p");           // iOS's copy, not taken in yet
    writeFile(folder + "/.Trash/Deleted.openshape", "p");         // Recently Deleted
    writeFile(folder + "/.hidden.openshape", "p");                // hidden
    writeFile(folder + "/notes.txt", "t");
    writeFile(folder + "/Bracket.openshape.tmp", "t");
    QDir().mkpath(folder + "/Folder.openshape");                  // a folder, not a file
    const QStringList projects = appFolderProjects(folder);
    ASSERT_EQ(projects.size(), 2) << projects.join(", ").toStdString();
    EXPECT_TRUE(projects[0].endsWith("/Bracket.openshape"));
    EXPECT_TRUE(projects[1].endsWith("/Upper.OPENSHAPE"));
    EXPECT_TRUE(appFolderProjects(QString()).isEmpty());
}

TEST(Purchase, FreeVersionAddsOneProject)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString folder = dir.path();
    const ProjectLimit freeVersion{true, false, folder};
    EXPECT_TRUE(limitAllowsProject(freeVersion)) << "the first project";
    writeFile(folder + "/Exports/Part.step", "s");
    EXPECT_TRUE(limitAllowsProject(freeVersion)) << "exports do not count";
    writeFile(folder + "/Part.openshape", "p");
    EXPECT_FALSE(limitAllowsProject(freeVersion)) << "a second one";
    EXPECT_FALSE(limitAllowsProject(freeVersion, folder + "/Other.openshape")) << "saving under a new name";
    EXPECT_TRUE(limitAllowsProject(freeVersion, folder + "/Part.openshape")) << "re-saving or replacing the one project";
    EXPECT_TRUE(limitAllowsProject(freeVersion, QDir(folder + "/Exports/..").path() + "/Part.openshape"))
        << "the same file by another spelling of its path";
}

TEST(Purchase, NoLimitUnlockedWithoutStoreOrFolder)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString folder = dir.path();
    for (const char* name : {"A", "B", "C"})
        writeFile(folder + "/" + name + ".openshape", "p");
    EXPECT_FALSE(limitAllowsProject({true, false, folder}));
    EXPECT_TRUE(limitAllowsProject({true, true, folder})) << "unlocked";
    EXPECT_TRUE(limitAllowsProject({false, false, folder})) << "no store: Windows, macOS";
    EXPECT_TRUE(limitAllowsProject({true, false, QString()})) << "no app folder";
    // Testers with several projects keep them; replacing one of them still adds nothing new,
    // but the others already fill the free version.
    EXPECT_FALSE(limitAllowsProject({true, false, folder}, folder + "/A.openshape"));
}

TEST(Purchase, DeletingTheProjectFreesTheSlot)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString folder = dir.path();
    writeFile(folder + "/Part.openshape", "p");
    EXPECT_FALSE(limitAllowsProject({true, false, folder}));
    ASSERT_TRUE(QDir(folder).mkpath(".Trash"));
    ASSERT_TRUE(QFile::rename(folder + "/Part.openshape", folder + "/.Trash/Part.openshape")); // as iOS's trash does
    EXPECT_TRUE(limitAllowsProject({true, false, folder}));
}

TEST(Purchase, UnlockIsRemembered)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    EXPECT_FALSE(loadProjectsUnlocked(settings));
    saveProjectsUnlocked(settings, true);
    settings.sync();
    QSettings again(dir.filePath("settings.ini"), QSettings::IniFormat);
    EXPECT_TRUE(loadProjectsUnlocked(again));
    saveProjectsUnlocked(again, false);
    EXPECT_FALSE(loadProjectsUnlocked(again));
}

TEST(Purchase, IncomingProjectAddsOnlyWhenNew)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IncomingPlaces places;
    places.appFolder = dir.filePath("Documents");
    places.stagingFolder = dir.filePath("staging");
    places.inboxes << places.appFolder + "/Inbox";
    QDir().mkpath(places.appFolder);
    const QString elsewhere = dir.filePath("iCloud Drive/Gear.openshape");
    writeFile(elsewhere, "gear project");
    EXPECT_TRUE(stagingAddsProject(elsewhere, places)) << "a project from elsewhere is copied in";
    writeFile(places.appFolder + "/Gear.openshape", "another gear");
    EXPECT_TRUE(stagingAddsProject(elsewhere, places)) << "a different project of that name: Gear 2";
    writeFile(places.appFolder + "/Gear 2.openshape", "gear project");
    EXPECT_FALSE(stagingAddsProject(elsewhere, places)) << "the same project is there already";
    EXPECT_FALSE(stagingAddsProject(places.appFolder + "/Gear.openshape", places)) << "opened where it is";
    const QString mailed = places.appFolder + "/Inbox/Mailed.openshape";
    writeFile(mailed, "mailed project");
    EXPECT_TRUE(stagingAddsProject(mailed, places)) << "moved out of the Inbox";
    const QString step = dir.filePath("iCloud Drive/Part.step");
    writeFile(step, "ISO-10303-21;");
    EXPECT_FALSE(stagingAddsProject(step, places)) << "a STEP file is not copied in as a project";
    EXPECT_FALSE(stagingAddsProject(dir.filePath("missing.openshape"), places));
    IncomingPlaces desktop;
    EXPECT_FALSE(stagingAddsProject(elsewhere, desktop)) << "no app folder: used where it is";
    // What stageIncomingFile then does agrees.
    const StagedFile staged = stageIncomingFile(mailed, places);
    EXPECT_TRUE(staged.created && staged.copied);
    const StagedFile again = stageIncomingFile(elsewhere, places);
    EXPECT_TRUE(again.copied && !again.created);
}

TEST(Purchase, SheetLoadsThePriceThenBuys)
{
    PurchaseFlow flow(unlockProductId(), false);
    EXPECT_EQ(flow.status(), PurchaseStatus::Loading);
    EXPECT_TRUE(flow.opening()) << "no price yet: ask the store";
    EXPECT_FALSE(flow.canBuy());
    EXPECT_FALSE(flow.startBuy());
    EXPECT_FALSE(flow.apply(event(StoreEvent::Kind::ProductLoaded, "$9.99")));
    EXPECT_EQ(flow.status(), PurchaseStatus::Ready);
    EXPECT_EQ(flow.price(), "$9.99");
    EXPECT_TRUE(flow.canBuy());
    EXPECT_FALSE(flow.opening()) << "the price is known";
    EXPECT_TRUE(flow.startBuy());
    EXPECT_EQ(flow.status(), PurchaseStatus::Buying);
    EXPECT_FALSE(flow.canBuy()) << "not twice";
    EXPECT_FALSE(flow.startRestore()) << "not while buying";
    flow.apply(event(StoreEvent::Kind::Purchasing));
    EXPECT_TRUE(flow.apply(event(StoreEvent::Kind::Purchased))) << "unlocked now";
    EXPECT_TRUE(flow.unlocked());
    EXPECT_FALSE(flow.canBuy());
    EXPECT_FALSE(flow.apply(event(StoreEvent::Kind::Purchased))) << "only once";
    EXPECT_FALSE(flow.opening());
}

TEST(Purchase, FailedCancelledAndWaiting)
{
    PurchaseFlow flow(unlockProductId(), false);
    flow.opening();
    flow.apply(event(StoreEvent::Kind::ProductLoaded, "9,99 €"));
    ASSERT_TRUE(flow.startBuy());
    flow.apply(event(StoreEvent::Kind::Failed, {}, "Cannot connect to iTunes Store"));
    EXPECT_EQ(flow.status(), PurchaseStatus::Failed);
    EXPECT_EQ(flow.message(), "The purchase did not go through: Cannot connect to iTunes Store");
    EXPECT_TRUE(flow.canBuy()) << "try again";
    EXPECT_FALSE(flow.unlocked());
    ASSERT_TRUE(flow.startBuy());
    EXPECT_TRUE(flow.message().isEmpty());
    flow.apply(event(StoreEvent::Kind::Cancelled));
    EXPECT_EQ(flow.status(), PurchaseStatus::Ready);
    EXPECT_TRUE(flow.message().isEmpty()) << "a cancel is no failure";
    ASSERT_TRUE(flow.startBuy());
    flow.apply(event(StoreEvent::Kind::Deferred));
    EXPECT_EQ(flow.status(), PurchaseStatus::Waiting);
    EXPECT_FALSE(flow.opening()) << "the sheet shows the wait again";
    EXPECT_EQ(flow.status(), PurchaseStatus::Waiting);
    // Approved later (maybe in a later run): it arrives unrequested and unlocks.
    EXPECT_TRUE(flow.apply(event(StoreEvent::Kind::Purchased)));
    EXPECT_TRUE(flow.unlocked());
}

TEST(Purchase, RestoreFindsOrDoesNot)
{
    PurchaseFlow flow(unlockProductId(), false);
    flow.apply(event(StoreEvent::Kind::ProductLoaded, "$9.99"));
    ASSERT_TRUE(flow.startRestore());
    EXPECT_EQ(flow.status(), PurchaseStatus::Restoring);
    EXPECT_FALSE(flow.canBuy());
    flow.apply(event(StoreEvent::Kind::RestoreFinished));
    EXPECT_EQ(flow.status(), PurchaseStatus::Ready);
    EXPECT_EQ(flow.message(), "No earlier purchase of unlimited projects was found for this Apple Account.");
    EXPECT_FALSE(flow.unlocked());
    ASSERT_TRUE(flow.startRestore());
    flow.apply(event(StoreEvent::Kind::RestoreFailed, {}, "offline"));
    EXPECT_EQ(flow.status(), PurchaseStatus::Failed);
    EXPECT_EQ(flow.message(), "The purchase could not be restored: offline");
    ASSERT_TRUE(flow.startRestore());
    EXPECT_TRUE(flow.apply(event(StoreEvent::Kind::Restored)));
    EXPECT_FALSE(flow.apply(event(StoreEvent::Kind::Restored))) << "several earlier transactions";
    flow.apply(event(StoreEvent::Kind::RestoreFinished));
    EXPECT_TRUE(flow.unlocked());
    EXPECT_TRUE(flow.message().isEmpty());
}

TEST(Purchase, StoreUnavailable)
{
    PurchaseFlow flow(unlockProductId(), false);
    ASSERT_TRUE(flow.opening());
    flow.apply(event(StoreEvent::Kind::ProductUnavailable));
    EXPECT_EQ(flow.status(), PurchaseStatus::Unavailable);
    EXPECT_FALSE(flow.canBuy()) << "Unlock disabled";
    EXPECT_TRUE(flow.message().contains("can't be reached")) << flow.message().toStdString();
    EXPECT_TRUE(flow.opening()) << "opened again: asks again";
    EXPECT_EQ(flow.status(), PurchaseStatus::Loading);
    flow.apply(event(StoreEvent::Kind::ProductUnavailable, {}, "Purchases are turned off on this device."));
    EXPECT_EQ(flow.message(), "Purchases are turned off on this device.");
    EXPECT_TRUE(flow.startRestore()) << "Restore still works without a price";
    flow.apply(event(StoreEvent::Kind::ProductLoaded, "$9.99"));
    EXPECT_EQ(flow.status(), PurchaseStatus::Restoring) << "a price does not end a restore";
    flow.apply(event(StoreEvent::Kind::Cancelled));
    EXPECT_EQ(flow.status(), PurchaseStatus::Ready);
    EXPECT_TRUE(flow.canBuy());
}

TEST(Purchase, OtherProductsChangeNothing)
{
    PurchaseFlow flow(unlockProductId(), false);
    flow.apply(event(StoreEvent::Kind::ProductLoaded, "$9.99"));
    EXPECT_FALSE(flow.apply(event(StoreEvent::Kind::Purchased, {}, {}, "com.example.other")));
    EXPECT_FALSE(flow.unlocked());
    flow.apply(event(StoreEvent::Kind::Failed, {}, "x", "com.example.other"));
    EXPECT_EQ(flow.status(), PurchaseStatus::Ready);
    EXPECT_TRUE(PurchaseFlow(unlockProductId(), true).unlocked()) << "remembered from an earlier run";
}

TEST(Purchase, StatusNames)
{
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Loading), "loading");
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Ready), "ready");
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Buying), "buying");
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Waiting), "waiting");
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Restoring), "restoring");
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Failed), "failed");
    EXPECT_EQ(purchaseStatusName(PurchaseStatus::Unavailable), "unavailable");
}
