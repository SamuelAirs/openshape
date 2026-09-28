// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the app remembers (ui/AppSettings), recovery sessions with real
// lock files (ui/RecoverySession), Home's preview sources
// (ui/ThumbnailSource) and where files from other apps go
// (ui/IncomingFiles). Qt Core only; no window.

#include "document/Feature.h"
#include "core/Uuid.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "io/ProjectFile.h"
#include "core/Version.h"
#include "ui/AppSettings.h"
#include "ui/Licenses.h"
#include "ui/IncomingFiles.h"
#include "ui/RecoverySession.h"
#include "ui/ThumbnailSource.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLockFile>
#include <QtCore/QSettings>
#include <QtCore/QSysInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QXmlStreamReader>

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
    EXPECT_DOUBLE_EQ(defaults.holeAllowance, 0.2) << "the FDM allowance the owner chose";

    savePreferences(settings, {LengthUnit::Inch, false, 0, 0.35});
    const Preferences loaded = loadPreferences(settings);
    EXPECT_EQ(loaded.defaultUnit, LengthUnit::Inch);
    EXPECT_FALSE(loaded.sketchGridSnap);
    EXPECT_EQ(loaded.recoveryIntervalSeconds, 0) << "recovery copies off";
    EXPECT_DOUBLE_EQ(loaded.holeAllowance, 0.35);
    savePreferences(settings, {LengthUnit::Inch, false, 0, 0.0});
    EXPECT_DOUBLE_EQ(loadPreferences(settings).holeAllowance, 0.0) << "no allowance: the standard sizes";

    // Stored values are untrusted: anything unexpected is the default.
    settings.setValue(QStringLiteral("preferences/defaultUnit"), QStringLiteral("furlong"));
    settings.setValue(QStringLiteral("preferences/recoveryIntervalSeconds"), 7);
    EXPECT_EQ(loadPreferences(settings).defaultUnit, LengthUnit::Millimeter);
    EXPECT_EQ(loadPreferences(settings).recoveryIntervalSeconds, 60);
    settings.setValue(QStringLiteral("preferences/recoveryIntervalSeconds"), QStringLiteral("soon"));
    EXPECT_EQ(loadPreferences(settings).recoveryIntervalSeconds, 60);
    for (int interval : kRecoveryIntervals) {
        savePreferences(settings, {LengthUnit::Millimeter, true, interval, 0.2});
        EXPECT_EQ(loadPreferences(settings).recoveryIntervalSeconds, interval);
    }
    for (const QVariant& bad : {QVariant(-0.1), QVariant(1.5), QVariant(QStringLiteral("wide")), QVariant(QStringLiteral("nan"))}) {
        settings.setValue(QStringLiteral("preferences/holeAllowanceMm"), bad);
        EXPECT_DOUBLE_EQ(loadPreferences(settings).holeAllowance, 0.2) << bad.toString().toStdString();
    }
}

// The projection toggle is remembered; a new install starts in perspective.
TEST(AppSettings, PerspectiveRoundTripAndDefault)
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    EXPECT_TRUE(loadPerspective(settings)) << "perspective by default";
    savePerspective(settings, false);
    EXPECT_FALSE(loadPerspective(settings));
    settings.sync();
    QSettings reread(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    EXPECT_FALSE(loadPerspective(reread)) << "kept across runs";
    savePerspective(settings, true);
    EXPECT_TRUE(loadPerspective(settings));
    // Stored values are untrusted: anything unexpected is the default.
    settings.setValue(QStringLiteral("view/perspective"), QStringLiteral("sideways"));
    EXPECT_TRUE(loadPerspective(settings));
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

TEST(AppSettings, ProjectNamesBecomeSafeFileNames)
{
    using os::ui::projectFileBaseName;
    EXPECT_EQ(projectFileBaseName(QStringLiteral("  Bracket  ")), QStringLiteral("Bracket"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("Bracket.openshape")), QStringLiteral("Bracket"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("Bracket.OPENSHAPE")), QStringLiteral("Bracket"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("a/b\\c:d*e?f\"g<h>i|j")), QStringLiteral("a-b-c-d-e-f-g-h-i-j"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("Lid v2 (20 mm)")), QStringLiteral("Lid v2 (20 mm)"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("Grüße 日本")), QStringLiteral("Grüße 日本"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("..")), QString());
    EXPECT_EQ(projectFileBaseName(QStringLiteral(".hidden")), QStringLiteral("hidden"));
    EXPECT_EQ(projectFileBaseName(QStringLiteral("   ")), QString());
    EXPECT_EQ(projectFileBaseName(QStringLiteral(".openshape")), QString());
    EXPECT_EQ(projectFileBaseName(QString(150, QLatin1Char('x'))).size(), 100);
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

// Qt hands an image provider the id as QQuickPixmap makes it:
// url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1), which
// decodes percent-encoded characters (a percent-encoded path came back with
// U+FFFD and '?' for every character outside ASCII: no preview on Home for
// any project under C:/Users/José).
QString providerId(const QString& source)
{
    return QUrl(source).toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
}

TEST(ThumbnailSource, AnyPathSurvivesQtsImageIds)
{
    const QStringList paths{
        QStringLiteral("C:/Users/sam/Projects/bracket.openshape"),
        QString::fromUtf8("C:/Users/Jos\xC3\xA9/My Files/\xD0\x96 50%.openshape"),
        QString::fromUtf8("/var/mobile/Documents/\xE6\x94\xAF\xE6\x9E\xB6 #2?.openshape"),
        QStringLiteral("C:/p/a%20b%2Fc+d&e=f.openshape"),
        QString::fromUtf8("C:/p/\xF0\x9F\x94\xA7 wrench.openshape"), // outside the BMP
    };
    for (const QString& path : paths) {
        const QString source = thumbnailSource(path, 1727000000123);
        const QUrl url(source);
        ASSERT_TRUE(url.isValid()) << source.toStdString();
        EXPECT_EQ(url.scheme(), QStringLiteral("image"));
        EXPECT_EQ(url.host(), QStringLiteral("thumbnail"));
        const QString id = providerId(source);
        EXPECT_TRUE(id.startsWith(QStringLiteral("1727000000123/"))) << id.toStdString();
        EXPECT_EQ(thumbnailPathFromId(id), path) << id.toStdString();
    }
    // Malformed ids give no path (the card shows "No preview").
    EXPECT_TRUE(thumbnailPathFromId(QStringLiteral("123")).isEmpty());
    EXPECT_TRUE(thumbnailPathFromId(QStringLiteral("123/")).isEmpty());
    EXPECT_TRUE(thumbnailPathFromId(QStringLiteral("123/not base64!")).isEmpty());
}

TEST(ThumbnailSource, PreviewOfAProjectWithANonAsciiName)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.path() + QString::fromUtf8("/Halterung f\xC3\xBCr Rad \xD0\x96 50%.openshape");
    doc::Document document;
    io::SaveOptions options;
    options.thumbnailPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13};
    ASSERT_TRUE(io::saveProject(document, std::filesystem::path(path.toStdWString()), options).ok());
    // What the provider does with the id Qt gives it.
    const QString back = thumbnailPathFromId(providerId(thumbnailSource(path, 42)));
    ASSERT_EQ(back, path);
    auto png = io::readProjectThumbnail(std::filesystem::path(back.toStdWString()));
    ASSERT_TRUE(png.ok()) << png.developerMessage();
    EXPECT_EQ(png.value(), options.thumbnailPng);
}

// ---- About -> Licenses (ui/Licenses; resources/licenses, built in) --------

TEST(Licenses, TheBuiltInListHasEveryLibraryWithItsLicense)
{
    const LicenseCatalog catalog = LicenseCatalog::load();
    ASSERT_TRUE(catalog.isValid()) << catalog.error().toStdString();
    const std::vector<std::pair<const char*, const char*>> expected{
        {"openshape", "MPL-2.0"},
        {"occt", "LGPL-2.1-only WITH OCCT-exception-1.0"},
        {"qt", "LGPL-3.0-only"},
        {"planegcs", "LGPL-2.1-or-later"},
        {"freetype", "FTL"},
        {"libzip", "BSD-3-Clause"},
        {"json", "MIT"},
        {"eigen", "MPL-2.0"},
        {"notosans", "OFL-1.1"},
    };
    for (const auto& [id, license] : expected) {
        const LicenseEntry* entry = catalog.find(QString::fromLatin1(id));
        ASSERT_NE(entry, nullptr) << id;
        EXPECT_EQ(entry->license, QString::fromLatin1(license)) << id;
        EXPECT_FALSE(entry->usedFor.isEmpty()) << id;
        EXPECT_FALSE(entry->source.isEmpty()) << id;
    }
    // The pinned versions (scripts/ios/sources.txt) of the libraries.
    EXPECT_EQ(catalog.find(QStringLiteral("qt"))->version, QStringLiteral("6.11.2"));
    EXPECT_EQ(catalog.find(QStringLiteral("occt"))->version, QStringLiteral("7.9.3"));
    EXPECT_EQ(catalog.find(QStringLiteral("freetype"))->version, QStringLiteral("2.14.3"));
    // Qt's third-party code, each with its license.
    int insideQt = 0;
    for (const LicenseEntry& entry : catalog.entries()) {
        if (entry.group != QStringLiteral("qt"))
            continue;
        ++insideQt;
        EXPECT_TRUE(entry.id.startsWith(QStringLiteral("qt:"))) << entry.id.toStdString();
        EXPECT_FALSE(entry.license.isEmpty()) << entry.id.toStdString();
        EXPECT_FALSE(entry.texts.empty()) << entry.id.toStdString();
    }
    EXPECT_GE(insideQt, 30);
    EXPECT_NE(catalog.find(QStringLiteral("qt:harfbuzz-ng")), nullptr);
    EXPECT_NE(catalog.find(QStringLiteral("qt:pcre2")), nullptr);
    // Not built into the iOS app: Wayland, Windows, Qt SQL.
    EXPECT_EQ(catalog.find(QStringLiteral("qt:wayland-protocol")), nullptr);
    EXPECT_EQ(catalog.find(QStringLiteral("qt:wintab")), nullptr);
    EXPECT_EQ(catalog.find(QStringLiteral("qt:sqlite")), nullptr);
}

TEST(Licenses, EveryEntryShowsItsFullTexts)
{
    const LicenseCatalog catalog = LicenseCatalog::load();
    ASSERT_TRUE(catalog.isValid()) << catalog.error().toStdString();
    qsizetype total = 0;
    for (const LicenseEntry& entry : catalog.entries()) {
        const QString text = catalog.text(entry.id);
        EXPECT_GT(text.size(), 100) << entry.id.toStdString();
        EXPECT_TRUE(text.startsWith(entry.name)) << entry.id.toStdString();
        for (const LicenseText& t : entry.texts)
            EXPECT_TRUE(text.contains(t.title)) << entry.id.toStdString() << ": " << t.title.toStdString();
        total += text.size();
    }
    EXPECT_GT(total, 200000); // the LGPL and GPL texts alone are ~70 kB
    // The LGPL 3.0 asks for the GPL 3.0 text too (4b); both come with Qt.
    const QString qt = catalog.text(QStringLiteral("qt"));
    EXPECT_TRUE(qt.contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
    EXPECT_TRUE(qt.contains(QStringLiteral("Version 3, 29 June 2007")));
    EXPECT_TRUE(qt.contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")));
    EXPECT_TRUE(qt.contains(QStringLiteral("Copyright (C) The Qt Company Ltd.")));
    // The Open CASCADE exception asks for this notice.
    const QString occt = catalog.text(QStringLiteral("occt"));
    EXPECT_TRUE(occt.contains(QStringLiteral("makes use of facilities provided by the Open CASCADE Technology software")));
    EXPECT_TRUE(occt.contains(QStringLiteral("Open CASCADE exception (version 1.0)")));
    EXPECT_TRUE(occt.contains(QStringLiteral("Version 2.1, February 1999")));
    // The FreeType License asks for this credit.
    EXPECT_TRUE(catalog.text(QStringLiteral("freetype")).contains(QStringLiteral("Portions of this software are copyright")));
    EXPECT_TRUE(catalog.text(QStringLiteral("openshape")).contains(QStringLiteral("Mozilla Public License Version 2.0")));
    EXPECT_TRUE(catalog.text(QStringLiteral("notosans")).contains(QStringLiteral("SIL OPEN FONT LICENSE Version 1.1")));
    EXPECT_TRUE(catalog.text(QStringLiteral("nothing")).isEmpty());
}

TEST(Licenses, TheSourceOfferNamesExactlyThisBuildsSource)
{
    const LicenseCatalog catalog = LicenseCatalog::load();
    ASSERT_TRUE(catalog.isValid()) << catalog.error().toStdString();

    // An App Store build: made from a release tag, with a CI build number.
    const BuildInfo release{QStringLiteral("1.2.3"), QStringLiteral("57"),
                            QStringLiteral("0123456789abcdef0123456789abcdef01234567"), QStringLiteral("v1.2.3-beta1"),
                            true};
    const QString offer = catalog.sourceOffer(release);
    EXPECT_TRUE(offer.contains(QStringLiteral("This is OpenShape 1.2.3 (build 57, release v1.2.3-beta1, commit 0123456789ab).")))
        << offer.toStdString();
    EXPECT_TRUE(offer.contains(QStringLiteral("https://github.com/SamuelAirs/openshape/tree/v1.2.3-beta1")));
    EXPECT_TRUE(offer.contains(QStringLiteral(
        "https://github.com/SamuelAirs/openshape/releases/tag/v1.2.3-beta1 (the file OpenShape-1.2.3-beta1-ios-sources.tar)")));
    EXPECT_TRUE(offer.contains(QStringLiteral("https://github.com/SamuelAirs/openshape/issues")));
    EXPECT_TRUE(offer.contains(QStringLiteral("Rebuilding the iOS app with modified libraries")));
    EXPECT_FALSE(offer.contains(QLatin1Char('@'))) << offer.toStdString();

    // A build between releases: the commit's tree and the pins in it.
    const BuildInfo commit{QStringLiteral("1.2.3"), QStringLiteral("1.2.3"),
                           QStringLiteral("abcdef0123456789abcdef0123456789abcdef01"), {}, true};
    const QString between = catalog.sourceOffer(commit);
    EXPECT_TRUE(between.contains(QStringLiteral("(commit abcdef012345)"))) << between.toStdString();
    EXPECT_TRUE(between.contains(QStringLiteral("https://github.com/SamuelAirs/openshape/tree/abcdef0123456789abcdef0123456789abcdef01")));
    EXPECT_TRUE(between.contains(QStringLiteral("scripts/ios/sources.txt")));
    EXPECT_FALSE(between.contains(QStringLiteral("/releases/tag/")));
    EXPECT_TRUE(between.contains(QStringLiteral("https://github.com/SamuelAirs/openshape/releases keeps copies")));

    // Nothing known (a build from a source archive without git).
    const BuildInfo unknown{QStringLiteral("1.2.3"), QStringLiteral("1.2.3"), {}, {}, false};
    EXPECT_EQ(sourceCodeUrl(unknown), QStringLiteral("https://github.com/SamuelAirs/openshape"));
    EXPECT_EQ(buildDescription(unknown), QStringLiteral("a local build"));

    // This build: CMake's version.
    EXPECT_EQ(BuildInfo::current().version, QString::fromLatin1(kAppVersion));
}

TEST(Licenses, TheDesktopOfferNamesTheDesktopPackagesSourcesNotTheIosApps)
{
    const LicenseCatalog catalog = LicenseCatalog::load();
    ASSERT_TRUE(catalog.isValid()) << catalog.error().toStdString();
    // The Windows package ships MSYS2's builds: their source is listed in
    // THIRD_PARTY_LICENSES.txt and attached to the release, not the iOS
    // app's pins or tar.
    const BuildInfo windows{QStringLiteral("1.2.3"), QStringLiteral("1.2.3"),
                            QStringLiteral("abcdef0123456789abcdef0123456789abcdef01"), {}, false};
    const QString offer = catalog.sourceOffer(windows);
    EXPECT_TRUE(offer.contains(QStringLiteral("THIRD_PARTY_LICENSES.txt beside the program lists each library in this package")))
        << offer.toStdString();
    EXPECT_TRUE(offer.contains(QStringLiteral("https://github.com/SamuelAirs/openshape/releases carries copies")));
    EXPECT_TRUE(offer.contains(QStringLiteral("the libraries and versions listed in this Licenses view are the iPhone and iPad app's")));
    EXPECT_FALSE(offer.contains(QStringLiteral("scripts/ios/sources.txt")));
    EXPECT_FALSE(offer.contains(QStringLiteral("/releases/tag/")));
    EXPECT_FALSE(offer.contains(QLatin1Char('@'))) << offer.toStdString();
    // Even with a release tag (never set on the desktop today), the iOS
    // tar is not named as this package's source.
    const BuildInfo tagged{QStringLiteral("1.2.3"), QStringLiteral("1.2.3"), {}, QStringLiteral("v1.2.3"), false};
    EXPECT_FALSE(catalog.sourceOffer(tagged).contains(QStringLiteral("(the file OpenShape-1.2.3-ios-sources.tar)")));
#ifdef Q_OS_IOS
    EXPECT_TRUE(BuildInfo::current().iosApp);
#else
    EXPECT_FALSE(BuildInfo::current().iosApp);
#endif
}

TEST(Licenses, ABrokenListIsReportedNotShownHalf)
{
    EXPECT_FALSE(LicenseCatalog::load(QStringLiteral(":/no/such/folder")).isValid());
    EXPECT_FALSE(LicenseCatalog::load(QStringLiteral(":/no/such/folder")).error().isEmpty());
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto write = [&dir](const QByteArray& json) {
        QFile file(dir.path() + QStringLiteral("/index.json"));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
    };
    write("{ not json");
    EXPECT_FALSE(LicenseCatalog::load(dir.path()).isValid());
    write(R"({"format": 2, "components": []})");
    EXPECT_FALSE(LicenseCatalog::load(dir.path()).isValid());
    write(R"({"format": 1, "components": [{"id": "x", "name": "X", "texts": []}]})");
    EXPECT_FALSE(LicenseCatalog::load(dir.path()).isValid());
    // A text file that is missing: the entry's page is empty, not half a page.
    write(R"({"format": 1, "components": [{"id": "x", "name": "X", "license": "MIT", "texts": [{"title": "MIT", "file": "gone.txt"}]}]})");
    const LicenseCatalog catalog = LicenseCatalog::load(dir.path());
    ASSERT_TRUE(catalog.isValid()) << catalog.error().toStdString();
    EXPECT_TRUE(catalog.text(QStringLiteral("x")).isEmpty());
    EXPECT_TRUE(catalog.sourceOffer(BuildInfo::current()).isEmpty()); // no source-offer.txt there
}

// ---- Files from other apps (ui/IncomingFiles) -----------------------------------------

namespace {

// A folder tree as on an iPhone: OpenShape's folder (Documents) with the
// system's Inbox, a scratch folder, and "elsewhere" (iCloud Drive, another
// app's folder).
struct IncomingFixture {
    QTemporaryDir root;
    IncomingPlaces places;
    QString elsewhere;

    IncomingFixture()
    {
        places.appFolder = root.path() + QStringLiteral("/Documents");
        places.stagingFolder = root.path() + QStringLiteral("/tmp/openshape-incoming");
        places.inboxes << places.appFolder + QStringLiteral("/Inbox");
        elsewhere = root.path() + QStringLiteral("/iCloud Drive");
        QDir().mkpath(places.appFolder);
        QDir().mkpath(elsewhere);
    }
    QString inbox() const { return places.inboxes.front(); }
};

// A real project file: a cube of the given size.
void writeProject(const QString& path, double size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    ASSERT_TRUE(io::saveProject(*cube(size), std::filesystem::path(path.toStdWString())).ok()) << path.toStdString();
}

void writeBytes(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QByteArray bytesOf(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

qsizetype projectsIn(const QString& folder)
{
    return QDir(folder).entryList({QStringLiteral("*.openshape")}, QDir::Files).size();
}

const QByteArray kStepText = "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n";

} // namespace

TEST(IncomingFiles, KindsByExtension)
{
    EXPECT_EQ(incomingKind(QStringLiteral("Bracket.openshape")), IncomingKind::Project);
    EXPECT_EQ(incomingKind(QStringLiteral("/x/y/LID.OPENSHAPE")), IncomingKind::Project);
    EXPECT_EQ(incomingKind(QStringLiteral("part.step")), IncomingKind::Step);
    EXPECT_EQ(incomingKind(QStringLiteral("part.STP")), IncomingKind::Step);
    EXPECT_EQ(incomingKind(QStringLiteral("part.stl")), IncomingKind::Unsupported);
    EXPECT_EQ(incomingKind(QStringLiteral("notes")), IncomingKind::Unsupported);
    EXPECT_EQ(incomingKind(QStringLiteral("step")), IncomingKind::Unsupported);
}

TEST(IncomingFiles, FoldersAndContents)
{
    IncomingFixture f;
    const QString docs = f.places.appFolder;
    EXPECT_TRUE(isInsideFolder(docs, docs));
    EXPECT_TRUE(isInsideFolder(docs + QStringLiteral("/a.openshape"), docs));
    EXPECT_TRUE(isInsideFolder(docs + QStringLiteral("/Exports/a.stl"), docs + QStringLiteral("/")));
    EXPECT_TRUE(isInsideFolder(docs + QStringLiteral("/Exports/../a.openshape"), docs));
    EXPECT_FALSE(isInsideFolder(docs + QStringLiteral("2/a.openshape"), docs)); // a sibling with a longer name
    EXPECT_FALSE(isInsideFolder(f.elsewhere + QStringLiteral("/a.openshape"), docs));
    EXPECT_FALSE(isInsideFolder(QString(), docs));
    EXPECT_FALSE(isInsideFolder(docs, QString()));
#if defined(Q_OS_WIN)
    EXPECT_TRUE(isInsideFolder(docs.toUpper() + QStringLiteral("/a.openshape"), docs));
#endif

    writeBytes(f.elsewhere + QStringLiteral("/a"), "same bytes");
    writeBytes(f.elsewhere + QStringLiteral("/b"), "same bytes");
    writeBytes(f.elsewhere + QStringLiteral("/c"), "other byte");
    writeBytes(f.elsewhere + QStringLiteral("/d"), "longer bytes here");
    EXPECT_TRUE(sameFileContents(f.elsewhere + QStringLiteral("/a"), f.elsewhere + QStringLiteral("/b")));
    EXPECT_FALSE(sameFileContents(f.elsewhere + QStringLiteral("/a"), f.elsewhere + QStringLiteral("/c")));
    EXPECT_FALSE(sameFileContents(f.elsewhere + QStringLiteral("/a"), f.elsewhere + QStringLiteral("/d")));
    EXPECT_FALSE(sameFileContents(f.elsewhere + QStringLiteral("/a"), f.elsewhere + QStringLiteral("/missing")));
}

TEST(IncomingFiles, OnTheDesktopFilesAreUsedWhereTheyAre)
{
    IncomingFixture f;
    IncomingPlaces desktop = f.places;
    desktop.appFolder.clear();
    const QString project = f.elsewhere + QStringLiteral("/Bracket.openshape");
    writeProject(project, 10);
    const StagedFile staged = stageIncomingFile(project, desktop);
    EXPECT_EQ(staged.kind, IncomingKind::Project);
    EXPECT_EQ(staged.path, QFileInfo(project).absoluteFilePath());
    EXPECT_FALSE(staged.copied);
    EXPECT_FALSE(staged.temporary);
    EXPECT_EQ(projectsIn(f.places.appFolder), 0);
}

TEST(IncomingFiles, ProjectFromElsewhereIsCopiedInOnce)
{
    IncomingFixture f;
    const QString project = f.elsewhere + QStringLiteral("/Bracket.openshape");
    writeProject(project, 10);
    const StagedFile first = stageIncomingFile(project, f.places);
    ASSERT_FALSE(first.path.isEmpty()) << first.error.toStdString();
    EXPECT_EQ(first.path, f.places.appFolder + QStringLiteral("/Bracket.openshape"));
    EXPECT_TRUE(first.copied);
    EXPECT_TRUE(first.created) << "a new file: removed again if it cannot be opened";
    EXPECT_FALSE(first.temporary);
    EXPECT_EQ(bytesOf(first.path), bytesOf(project));
    EXPECT_TRUE(QFileInfo::exists(project)) << "the original stays where it was";
    EXPECT_NEAR(copyVolume(std::filesystem::path(first.path.toStdWString())), 1000.0, 1e-6);

    // The same file again: the copy there is used, no second one.
    const StagedFile again = stageIncomingFile(project, f.places);
    EXPECT_EQ(again.path, first.path);
    EXPECT_TRUE(again.copied);
    EXPECT_FALSE(again.created) << "the copy that was there already is never removed";
    EXPECT_EQ(projectsIn(f.places.appFolder), 1);

    // A different project of the same name: "Bracket 2", then "Bracket 3".
    const QString other = f.elsewhere + QStringLiteral("/other/Bracket.openshape");
    writeProject(other, 20);
    const StagedFile second = stageIncomingFile(other, f.places);
    EXPECT_EQ(second.path, f.places.appFolder + QStringLiteral("/Bracket 2.openshape"));
    EXPECT_TRUE(second.created);
    EXPECT_NEAR(copyVolume(std::filesystem::path(second.path.toStdWString())), 8000.0, 1e-6);
    const QString third = f.elsewhere + QStringLiteral("/third/Bracket.openshape");
    writeProject(third, 30);
    EXPECT_EQ(stageIncomingFile(third, f.places).path, f.places.appFolder + QStringLiteral("/Bracket 3.openshape"));
    // ...and the second one again finds its copy.
    EXPECT_EQ(stageIncomingFile(other, f.places).path, second.path);
    EXPECT_EQ(projectsIn(f.places.appFolder), 3);
}

TEST(IncomingFiles, ProjectInTheAppFolderIsOpenedInPlace)
{
    IncomingFixture f;
    for (const QString& path : {f.places.appFolder + QStringLiteral("/Lid.openshape"),
                                f.places.appFolder + QStringLiteral("/Designs/Lid.openshape")}) {
        writeProject(path, 10);
        const StagedFile staged = stageIncomingFile(path, f.places);
        EXPECT_EQ(staged.path, QFileInfo(path).absoluteFilePath());
        EXPECT_FALSE(staged.copied);
        EXPECT_FALSE(staged.created) << "the user's own project is never removed";
    }
    EXPECT_EQ(projectsIn(f.places.appFolder), 1);
}

TEST(IncomingFiles, InboxCopiesAreMovedOut)
{
    IncomingFixture f;
    // Mail's attachment: iOS put a copy into the app's Inbox.
    const QString mailed = f.inbox() + QStringLiteral("/Hinge.openshape");
    writeProject(mailed, 10);
    const QByteArray bytes = bytesOf(mailed);
    const StagedFile staged = stageIncomingFile(mailed, f.places);
    EXPECT_EQ(staged.path, f.places.appFolder + QStringLiteral("/Hinge.openshape"));
    EXPECT_TRUE(staged.copied);
    EXPECT_TRUE(staged.created);
    EXPECT_EQ(bytesOf(staged.path), bytes);
    EXPECT_FALSE(QFileInfo::exists(mailed)) << "moved, not copied";
    EXPECT_FALSE(QFileInfo::exists(f.inbox())) << "an empty Inbox is removed";
    EXPECT_TRUE(QFileInfo(staged.path).isWritable());

    // The same project mailed again: the one there is used, the Inbox copy removed.
    writeBytes(f.inbox() + QStringLiteral("/other.txt"), "stays");
    ASSERT_TRUE(QFile::copy(staged.path, mailed));
    const StagedFile mailedAgain = stageIncomingFile(mailed, f.places);
    EXPECT_EQ(mailedAgain.path, staged.path);
    EXPECT_FALSE(mailedAgain.created);
    EXPECT_FALSE(QFileInfo::exists(mailed));
    EXPECT_TRUE(QFileInfo::exists(f.inbox())) << "an Inbox with other files stays";
    EXPECT_EQ(projectsIn(f.places.appFolder), 1);

    // A STEP file from the Inbox: moved to the scratch folder.
    const QString step = f.inbox() + QStringLiteral("/Bracket.step");
    writeBytes(step, kStepText);
    const StagedFile stepStaged = stageIncomingFile(step, f.places);
    EXPECT_EQ(stepStaged.kind, IncomingKind::Step);
    EXPECT_EQ(stepStaged.path, f.places.stagingFolder + QStringLiteral("/Bracket.step"));
    EXPECT_TRUE(stepStaged.temporary);
    EXPECT_EQ(bytesOf(stepStaged.path), kStepText);
    EXPECT_FALSE(QFileInfo::exists(step));
}

TEST(IncomingFiles, StepFilesAreReadFromAScratchCopy)
{
    IncomingFixture f;
    const QString step = f.elsewhere + QStringLiteral("/Motor mount.STP");
    writeBytes(step, kStepText);
    const StagedFile staged = stageIncomingFile(step, f.places);
    ASSERT_FALSE(staged.path.isEmpty()) << staged.error.toStdString();
    EXPECT_EQ(staged.kind, IncomingKind::Step);
    EXPECT_EQ(staged.path, f.places.stagingFolder + QStringLiteral("/Motor mount.STP"));
    EXPECT_TRUE(staged.temporary);
    EXPECT_FALSE(staged.copied);
    EXPECT_FALSE(staged.created);
    EXPECT_EQ(bytesOf(staged.path), kStepText);
    EXPECT_TRUE(QFileInfo::exists(step));
    EXPECT_EQ(QDir(f.places.appFolder).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 0) << "nothing in OpenShape's folder";

    // A changed file with the same name replaces the old scratch copy.
    writeBytes(step, kStepText + "/* changed */\n");
    EXPECT_EQ(bytesOf(stageIncomingFile(step, f.places).path), kStepText + "/* changed */\n");
    // The scratch copy itself is used as it is.
    const StagedFile again = stageIncomingFile(staged.path, f.places);
    EXPECT_EQ(again.path, staged.path);
    EXPECT_TRUE(QFileInfo::exists(staged.path));

    // A STEP file in OpenShape's folder is read where it is (and kept).
    const QString inFolder = f.places.appFolder + QStringLiteral("/Exports/Lid.step");
    writeBytes(inFolder, kStepText);
    const StagedFile local = stageIncomingFile(inFolder, f.places);
    EXPECT_EQ(local.path, QFileInfo(inFolder).absoluteFilePath());
    EXPECT_FALSE(local.temporary);
}

TEST(IncomingFiles, WhatCannotBeUsedIsSaidPlainly)
{
    IncomingFixture f;
    const QString text = f.elsewhere + QStringLiteral("/notes.txt");
    writeBytes(text, "hello");
    const StagedFile unsupported = stageIncomingFile(text, f.places);
    EXPECT_TRUE(unsupported.path.isEmpty());
    EXPECT_EQ(unsupported.kind, IncomingKind::Unsupported);
    EXPECT_EQ(unsupported.error.toStdString(),
              "OpenShape opens projects (.openshape) and STEP files (.step, .stp); \xE2\x80\x9Cnotes.txt\xE2\x80\x9D is neither.");

    const StagedFile missing = stageIncomingFile(f.elsewhere + QStringLiteral("/gone.openshape"), f.places);
    EXPECT_TRUE(missing.path.isEmpty());
    EXPECT_EQ(missing.error.toStdString(), "\xE2\x80\x9Cgone.openshape\xE2\x80\x9D could not be read.");
    EXPECT_EQ(projectsIn(f.places.appFolder), 0);

    // The kind can be given (the Open picker only offers projects).
    const QString odd = f.elsewhere + QStringLiteral("/backup.bin");
    writeProject(odd, 10);
    const StagedFile forced = stageIncomingFile(odd, f.places, IncomingKind::Project);
    EXPECT_EQ(forced.path, f.places.appFolder + QStringLiteral("/backup.openshape"));
}

namespace {

// The iOS Info.plist template as nested QVariants (dict, array, string, bool).
QVariant readPlistValue(QXmlStreamReader& xml)
{
    const QString tag = xml.name().toString();
    if (tag == QLatin1String("dict")) {
        QVariantMap map;
        QString key;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("key"))
                key = xml.readElementText();
            else
                map.insert(key, readPlistValue(xml));
        }
        return map;
    }
    if (tag == QLatin1String("array")) {
        QVariantList list;
        while (xml.readNextStartElement())
            list.append(readPlistValue(xml));
        return list;
    }
    if (tag == QLatin1String("true") || tag == QLatin1String("false")) {
        xml.skipCurrentElement();
        return tag == QLatin1String("true");
    }
    return xml.readElementText();
}

QVariantMap iosInfoPlist()
{
    QFile file(QStringLiteral(OPENSHAPE_SOURCE_DIR "/src/app/ios/Info.plist.in"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QXmlStreamReader xml(&file);
    while (xml.readNextStartElement())
        if (xml.name() == QLatin1String("dict"))
            return readPlistValue(xml).toMap();
    return {};
}

} // namespace

// What iOS offers to hand to OpenShape (Info.plist) is what AppController
// takes (incomingKind): each document type names a declared type whose
// extensions are the ones that kind has.
TEST(IncomingFiles, InfoPlistDocumentTypesMatchWhatOpenShapeTakes)
{
    const QVariantMap plist = iosInfoPlist();
    ASSERT_FALSE(plist.isEmpty()) << "src/app/ios/Info.plist.in could not be read";
    EXPECT_TRUE(plist.value(QStringLiteral("LSSupportsOpeningDocumentsInPlace")).toBool());
    // Declared types: identifier -> (extensions, conforms to).
    QMap<QString, QPair<QStringList, QStringList>> declared;
    for (const char* key : {"UTExportedTypeDeclarations", "UTImportedTypeDeclarations"})
        for (const QVariant& type : plist.value(QString::fromLatin1(key)).toList()) {
            const QVariantMap t = type.toMap();
            declared.insert(t.value(QStringLiteral("UTTypeIdentifier")).toString(),
                            {t.value(QStringLiteral("UTTypeTagSpecification")).toMap().value(QStringLiteral("public.filename-extension")).toStringList(),
                             t.value(QStringLiteral("UTTypeConformsTo")).toStringList()});
        }
    const QVariantList documentTypes = plist.value(QStringLiteral("CFBundleDocumentTypes")).toList();
    ASSERT_EQ(documentTypes.size(), 2);
    const std::pair<const char*, IncomingKind> expected[] = {{"Owner", IncomingKind::Project}, {"Alternate", IncomingKind::Step}};
    for (int i = 0; i < 2; ++i) {
        const QVariantMap type = documentTypes[i].toMap();
        EXPECT_EQ(type.value(QStringLiteral("LSHandlerRank")).toString().toStdString(), expected[i].first);
        EXPECT_FALSE(type.value(QStringLiteral("CFBundleTypeName")).toString().isEmpty());
        const QStringList utis = type.value(QStringLiteral("LSItemContentTypes")).toStringList();
        ASSERT_EQ(utis.size(), 1);
        ASSERT_TRUE(declared.contains(utis.front())) << utis.front().toStdString() << " is not declared";
        const auto& [extensions, conforms] = declared.value(utis.front());
        EXPECT_TRUE(conforms.contains(QStringLiteral("public.data")));
        ASSERT_FALSE(extensions.isEmpty());
        for (const QString& extension : extensions)
            EXPECT_EQ(incomingKind(QStringLiteral("file.") + extension), expected[i].second) << extension.toStdString();
    }
    EXPECT_EQ(declared.value(QStringLiteral("io.github.samuelairs.openshape.project")).first, QStringList{QStringLiteral("openshape")});
    EXPECT_EQ(declared.value(QStringLiteral("org.iso.step")).first, (QStringList{QStringLiteral("step"), QStringLiteral("stp")}));
}

TEST(IncomingFiles, CopiesGetSafeNames)
{
    IncomingFixture f;
    // A leading dot would hide the copy in the Files app.
    const QString hidden = f.elsewhere + QStringLiteral("/.hidden.openshape");
    writeProject(hidden, 10);
    EXPECT_EQ(stageIncomingFile(hidden, f.places).path, f.places.appFolder + QStringLiteral("/hidden.openshape"));
    // Nothing usable left: "Untitled".
    const QString dots = f.elsewhere + QStringLiteral("/...openshape");
    writeProject(dots, 20);
    EXPECT_EQ(stageIncomingFile(dots, f.places).path, f.places.appFolder + QStringLiteral("/Untitled.openshape"));
    // Names outside ASCII stay as they are.
    const QString name = f.elsewhere + QString::fromUtf8("/Halterung f\xC3\xBCr Rad \xD0\x96.openshape");
    writeProject(name, 30);
    EXPECT_EQ(stageIncomingFile(name, f.places).path,
              f.places.appFolder + QString::fromUtf8("/Halterung f\xC3\xBCr Rad \xD0\x96.openshape"));
}
