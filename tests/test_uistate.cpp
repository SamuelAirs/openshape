// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the app remembers (ui/AppSettings) and recovery sessions with real
// lock files (ui/RecoverySession). Qt Core only; no window.

#include "document/Feature.h"
#include "core/Uuid.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "io/ProjectFile.h"
#include "ui/AppSettings.h"
#include "ui/RecoverySession.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLockFile>
#include <QtCore/QSettings>
#include <QtCore/QSysInfo>
#include <QtCore/QTemporaryDir>

#include <gtest/gtest.h>

using namespace os;
using namespace os::ui;

namespace {

// QLockFile records the application's name and pid: give it an application.
class QtEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        static int argc = 1;
        static char name[] = "test_uistate";
        static char* argv[] = {name, nullptr};
        app_ = std::make_unique<QCoreApplication>(argc, argv);
    }
    void TearDown() override { app_.reset(); }

private:
    std::unique_ptr<QCoreApplication> app_;
};
const auto* const environment = ::testing::AddGlobalTestEnvironment(new QtEnvironment);

std::unique_ptr<doc::Document> cube(double size)
{
    auto d = std::make_unique<doc::Document>();
    auto body = std::make_unique<doc::Body>();
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {size, size, size};
    body->insertFeature(std::move(box), 0);
    d->addBody(std::move(body));
    return d;
}

// The lock file a crashed OpenShape leaves: a process id that no longer runs.
void writeDeadLock(const QString& path)
{
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("2147483644\nOpenShape.exe\n" + QSysInfo::machineHostName().toUtf8() + "\n");
}

QString lockOf(const RecoverySession& s, const std::string& session)
{
    return QString::fromStdWString(s.store().lockFile(session).wstring());
}

double copyVolume(const std::filesystem::path& file)
{
    auto loaded = io::loadProject(file);
    return loaded && !loaded.value()->bodies().empty() ? geom::volume(loaded.value()->bodies().front()->shape()) : -1;
}

} // namespace

// ---- Preferences, recent files, window place ------------------------------------------

TEST(AppSettings, PreferencesRoundTripAndDefaults)
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    const Preferences defaults = loadPreferences(settings);
    EXPECT_EQ(defaults.defaultUnit, LengthUnit::Millimeter);
    EXPECT_TRUE(defaults.sketchGridSnap);
    EXPECT_EQ(defaults.recoveryIntervalSeconds, 60);

    savePreferences(settings, {LengthUnit::Inch, false, 0});
    const Preferences loaded = loadPreferences(settings);
    EXPECT_EQ(loaded.defaultUnit, LengthUnit::Inch);
    EXPECT_FALSE(loaded.sketchGridSnap);
    EXPECT_EQ(loaded.recoveryIntervalSeconds, 0) << "recovery copies off";

    // Stored values are untrusted: anything unexpected is the default.
    settings.setValue(QStringLiteral("preferences/defaultUnit"), QStringLiteral("furlong"));
    settings.setValue(QStringLiteral("preferences/recoveryIntervalSeconds"), 7);
    EXPECT_EQ(loadPreferences(settings).defaultUnit, LengthUnit::Millimeter);
    EXPECT_EQ(loadPreferences(settings).recoveryIntervalSeconds, 60);
    settings.setValue(QStringLiteral("preferences/recoveryIntervalSeconds"), QStringLiteral("soon"));
    EXPECT_EQ(loadPreferences(settings).recoveryIntervalSeconds, 60);
    for (int interval : kRecoveryIntervals) {
        savePreferences(settings, {LengthUnit::Millimeter, true, interval});
        EXPECT_EQ(loadPreferences(settings).recoveryIntervalSeconds, interval);
    }
}

TEST(AppSettings, RecentFilesAndWindowPlacementRoundTrip)
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    EXPECT_TRUE(loadRecentFiles(settings).isEmpty());
    saveRecentFiles(settings, {QStringLiteral("C:/a.openshape"), QString(), QStringLiteral("C:/b.openshape")});
    EXPECT_EQ(loadRecentFiles(settings), (QStringList{QStringLiteral("C:/a.openshape"), QStringLiteral("C:/b.openshape")}));

    EXPECT_FALSE(loadWindowPlacement(settings).has_value()) << "nothing saved yet";
    saveWindowPlacement(settings, {QRect(100, 80, 1208, 939), QRect(104, 111, 1200, 904), true});
    const auto placement = loadWindowPlacement(settings);
    ASSERT_TRUE(placement.has_value());
    EXPECT_EQ(placement->frame, QRect(100, 80, 1208, 939));
    EXPECT_EQ(placement->client, QRect(104, 111, 1200, 904));
    EXPECT_TRUE(placement->maximized);
    saveWindowPlacement(settings, {QRect(0, 0, 100, 100), QRect(0, 0, 300, 300), false});
    EXPECT_FALSE(loadWindowPlacement(settings).has_value()) << "a client area outside its frame is nonsense";
}

TEST(AppSettings, FirstWindowFitsSmallOrScaledScreens)
{
    const QSize preferred(1400, 900), minimum(720, 480);
    EXPECT_TRUE(firstWindowGeometry(preferred, QRect(0, 0, 1920, 1032), minimum, 40).isNull()) << "fits: the default";
    // 1920x1080 at 150 %: 1280x688 logical available.
    const QRect scaled = firstWindowGeometry(preferred, QRect(0, 0, 1280, 688), minimum, 40);
    EXPECT_EQ(scaled.size(), QSize(1152, 583));
    EXPECT_TRUE(QRect(0, 40, 1280, 648).contains(scaled)) << "below the title bar, on the screen";
    EXPECT_EQ(scaled.center().x(), 639);
    // Smaller than the minimum: as large as the screen allows.
    const QRect tiny = firstWindowGeometry(preferred, QRect(0, 0, 800, 500), minimum, 40);
    EXPECT_EQ(tiny.size(), QSize(720, 460));
    EXPECT_TRUE(QRect(0, 40, 800, 460).contains(tiny));
}

TEST(AppSettings, ClientAreaKeepsTheFramesBorders)
{
    // Windows 11: a 31 px title bar, no side borders. Restoring places the
    // client area; saving reads the frame. The two must not drift apart.
    const WindowPlacement saved{QRect(700, 160, 1200, 831), QRect(700, 191, 1200, 800), false};
    EXPECT_EQ(clientForFrame(saved, saved.frame), saved.client);
    EXPECT_EQ(clientForFrame(saved, QRect(0, 0, 1200, 831)), QRect(0, 31, 1200, 800));
    // Windows 10 style: 8 px borders all round.
    const WindowPlacement bordered{QRect(92, 49, 1216, 839), QRect(100, 80, 1200, 800), false};
    EXPECT_EQ(clientForFrame(bordered, QRect(0, 0, 1000, 700)), QRect(8, 31, 984, 661));
}

TEST(AppSettings, WindowIsNeverRestoredOffScreen)
{
    const QSize minimum(720, 480);
    const QRect primary(0, 0, 1920, 1040); // available area (task bar excluded)
    const QRect second(1920, 0, 2560, 1400);

    // Where it was, if that still fits.
    EXPECT_EQ(fitToScreens(QRect(100, 50, 1400, 900), {primary}, minimum), QRect(100, 50, 1400, 900));
    EXPECT_EQ(fitToScreens(QRect(2200, 100, 1400, 900), {primary, second}, minimum), QRect(2200, 100, 1400, 900));
    // The second monitor is gone: onto the primary, fully visible.
    const QRect moved = fitToScreens(QRect(2200, 100, 1400, 900), {primary}, minimum);
    EXPECT_TRUE(primary.contains(moved)) << moved.x() << "," << moved.y();
    EXPECT_EQ(moved.size(), QSize(1400, 900));
    // The resolution went down: smaller, inside.
    const QRect small(0, 0, 1280, 680);
    const QRect shrunk = fitToScreens(QRect(300, 200, 1900, 1000), {small}, minimum);
    EXPECT_TRUE(small.contains(shrunk));
    EXPECT_EQ(shrunk.size(), QSize(1280, 680));
    // Partly off the left and top edges: the title bar comes back on screen.
    EXPECT_EQ(fitToScreens(QRect(-300, -40, 1000, 700), {primary}, minimum), QRect(0, 0, 1000, 700));
    // Straddling two screens: the one it overlaps most.
    const QRect straddle = fitToScreens(QRect(1700, 100, 1000, 700), {primary, second}, minimum);
    EXPECT_TRUE(second.contains(straddle));
    // Screens to the left of the primary have negative coordinates.
    const QRect left(-1280, 0, 1280, 1024);
    EXPECT_EQ(fitToScreens(QRect(-1200, 40, 900, 600), {primary, left}, minimum), QRect(-1200, 40, 900, 600));
    // Never below the minimum size unless the screen itself is smaller.
    EXPECT_EQ(fitToScreens(QRect(10, 10, 200, 100), {primary}, minimum).size(), minimum);
    EXPECT_EQ(fitToScreens(QRect(0, 0, 900, 600), {QRect(0, 0, 640, 400)}, minimum).size(), QSize(640, 400));
    EXPECT_TRUE(fitToScreens(QRect(), {primary}, minimum).isNull()) << "nothing saved";
    EXPECT_TRUE(fitToScreens(QRect(0, 0, 800, 600), {}, minimum).isNull()) << "no screens known";
}

// ---- Recovery sessions --------------------------------------------------------------

TEST(RecoverySession, LiveSessionsAreNeverOffered)
{
    QTemporaryDir dir;
    auto a = std::make_unique<RecoverySession>(dir.path());
    RecoverySession b(dir.path());
    ASSERT_TRUE(a->isLocked());
    ASSERT_TRUE(b.isLocked());
    EXPECT_NE(a->session(), b.session());
    ASSERT_TRUE(a->write(*cube(10), {"", "Untitled", 0, "0.1.0"}).ok());
    EXPECT_TRUE(a->hasCopy());
    EXPECT_TRUE(b.findOrphans().empty()) << "a is running: its copy is not b's to offer";
    EXPECT_FALSE(b.discard(a->session()).ok()) << "and not b's to delete";
    EXPECT_TRUE(a->hasCopy());

    // A clean exit removes the copy and the lock.
    const QString aLock = lockOf(*a, a->session());
    EXPECT_TRUE(QFile::exists(aLock));
    a.reset();
    EXPECT_FALSE(QFile::exists(aLock));
    EXPECT_TRUE(b.findOrphans().empty()) << "nothing left behind";
}

TEST(RecoverySession, CrashedSessionIsOfferedOnceAndCanBeRestored)
{
    QTemporaryDir dir;
    RecoverySession b(dir.path());
    // A crashed session: its copy, its sidecar and a lock whose process is gone.
    const std::string crashed = Uuid::generate().toString();
    ASSERT_TRUE(b.store().write(crashed, *cube(20), {"C:/parts/cube.openshape", "cube", 0, "0.1.0"}).ok());
    writeDeadLock(lockOf(b, crashed));

    const auto orphans = b.findOrphans();
    ASSERT_EQ(orphans.size(), 1u);
    EXPECT_EQ(orphans[0].session, crashed);
    EXPECT_EQ(orphans[0].info.title, "cube");
    EXPECT_EQ(orphans[0].info.originalPath, "C:/parts/cube.openshape");
    EXPECT_NEAR(copyVolume(orphans[0].projectFile), 8000.0, 1e-6);

    // A second instance starting meanwhile does not offer it too (b holds its lock).
    {
        RecoverySession c(dir.path());
        EXPECT_TRUE(c.findOrphans().empty());
    }

    // Restore: the copy becomes b's and stays until saved or discarded.
    ASSERT_TRUE(b.adopt(crashed, orphans[0].info).ok());
    EXPECT_TRUE(b.hasCopy());
    EXPECT_NEAR(copyVolume(b.store().projectFile(b.session())), 8000.0, 1e-6);
    EXPECT_FALSE(b.store().exists(crashed));
    EXPECT_FALSE(QFile::exists(lockOf(b, crashed))) << "the dead session's lock is gone";
    RecoverySession c(dir.path());
    EXPECT_TRUE(c.findOrphans().empty()) << "b is running: the adopted copy is b's now";
    b.removeCopy(); // saved
    EXPECT_FALSE(b.hasCopy());
}

TEST(RecoverySession, KeptCopyIsOfferedAtTheNextStart)
{
    // The run ends with unsaved work the user did not discard (iPadOS ended
    // the app, Windows logged off): the copy and its sidecar stay, the lock goes.
    QTemporaryDir dir;
    auto a = std::make_unique<RecoverySession>(dir.path());
    ASSERT_TRUE(a->write(*cube(20), {"C:/parts/cube.openshape", "cube", 0, "0.1.0"}).ok());
    const std::string session = a->session();
    const QString lock = lockOf(*a, session);
    a->setKeepCopy(true);
    a.reset();
    EXPECT_FALSE(QFile::exists(lock)) << "the lock is released";

    RecoverySession next(dir.path());
    const auto orphans = next.findOrphans();
    ASSERT_EQ(orphans.size(), 1u) << "offered like a crashed run's copy";
    EXPECT_EQ(orphans[0].session, session);
    EXPECT_EQ(orphans[0].info.originalPath, "C:/parts/cube.openshape");
    EXPECT_NEAR(copyVolume(orphans[0].projectFile), 8000.0, 1e-6);

    // Without the flag (saved, or Don't Save) the copy goes with the session.
    auto b = std::make_unique<RecoverySession>(dir.path());
    ASSERT_TRUE(b->write(*cube(5), {}).ok());
    ASSERT_TRUE(b->hasCopy());
    const auto bCopy = b->store().projectFile(b->session());
    b->setKeepCopy(true);
    b->setKeepCopy(false);
    b.reset();
    EXPECT_FALSE(std::filesystem::exists(bCopy));
}

TEST(RecoverySession, DiscardAndDecideLater)
{
    QTemporaryDir dir;
    const std::string first = Uuid::generate().toString(), second = Uuid::generate().toString();
    {
        RecoverySession writer(dir.path());
        for (const auto& s : {first, second}) {
            ASSERT_TRUE(writer.store().write(s, *cube(5), {"", "Untitled", 0, "0.1.0"}).ok());
            writeDeadLock(lockOf(writer, s));
        }
    }
    auto b = std::make_unique<RecoverySession>(dir.path());
    ASSERT_EQ(b->findOrphans().size(), 2u);
    ASSERT_TRUE(b->discard(first).ok());
    EXPECT_FALSE(b->store().exists(first));
    EXPECT_FALSE(QFile::exists(lockOf(*b, first)));

    // Decide later: the copy stays, and the next start offers it again.
    b->releaseOrphans();
    EXPECT_TRUE(b->store().exists(second));
    b.reset();
    RecoverySession next(dir.path());
    const auto again = next.findOrphans();
    ASSERT_EQ(again.size(), 1u);
    EXPECT_EQ(again[0].session, second);
}

TEST(RecoverySession, CleansUpAfterCrashesWithNothingUnsaved)
{
    QTemporaryDir dir;
    const std::string dead = Uuid::generate().toString();
    RecoverySession b(dir.path());
    writeDeadLock(lockOf(b, dead)); // crashed with a clean document: only its lock is left
    QFile half(QString::fromStdWString(b.store().projectFile(dead).wstring()) + QStringLiteral(".tmp"));
    ASSERT_TRUE(half.open(QIODevice::WriteOnly));
    half.write("half a zip");
    half.close();
    EXPECT_TRUE(b.findOrphans().empty());
    EXPECT_FALSE(QFile::exists(lockOf(b, dead)));
    EXPECT_FALSE(half.exists());
    EXPECT_TRUE(QFile::exists(lockOf(b, b.session()))) << "our own lock stays";
}
