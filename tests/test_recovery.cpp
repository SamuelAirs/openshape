// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Recovery copies (io/Recovery) and the recent-files list (io/RecentFiles).

#include "TestHelpers.h"

#include "io/ProjectFile.h"
#include "io/RecentFiles.h"
#include "io/Recovery.h"

#include <filesystem>
#include <fstream>
#include <set>

using namespace os;
using namespace os::test;

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("openshape_test_recovery_" + name))
    {
        std::filesystem::remove_all(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::unique_ptr<doc::Document> cubeDocument(double size)
{
    auto d = std::make_unique<doc::Document>();
    auto body = std::make_unique<doc::Body>();
    body->setName("Cube");
    body->insertFeature(boxFeature(size, size, size), 0);
    d->addBody(std::move(body));
    return d;
}

void writeText(const std::filesystem::path& p, const std::string& text)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string session() { return Uuid::generate().toString(); }

std::set<std::string> sessionsOf(const std::vector<io::RecoveryEntry>& entries)
{
    std::set<std::string> out;
    for (const auto& e : entries)
        out.insert(e.session);
    return out;
}

} // namespace

TEST(Recovery, SessionNames)
{
    EXPECT_TRUE(io::isRecoverySessionName(Uuid::generate().toString()));
    EXPECT_FALSE(io::isRecoverySessionName(""));
    EXPECT_FALSE(io::isRecoverySessionName("../../etc/passwd"));
    EXPECT_FALSE(io::isRecoverySessionName("0123456789abcdef0123456789abcdef0123")) << "no dashes";
    EXPECT_FALSE(io::isRecoverySessionName("0123456A-89ab-cdef-0123-456789abcdef")) << "uppercase";
    EXPECT_TRUE(io::isRecoverySessionName("01234567-89ab-cdef-0123-456789abcdef"));
}

TEST(Recovery, SidecarRoundTripAndValidation)
{
    io::RecoveryInfo info;
    info.originalPath = "C:/Users/me/Documents/br\xC3\xA4" "cket.openshape"; // UTF-8 umlaut
    info.title = "br\xC3\xA4" "cket";
    info.savedAtMs = 1'790'000'000'123;
    info.appVersion = "0.1.0";
    const auto parsed = io::parseRecoverySidecar(io::recoverySidecarJson(info));
    ASSERT_TRUE(parsed.ok()) << parsed.developerMessage();
    EXPECT_EQ(parsed.value().originalPath, info.originalPath);
    EXPECT_EQ(parsed.value().title, info.title);
    EXPECT_EQ(parsed.value().savedAtMs, info.savedAtMs);
    EXPECT_EQ(parsed.value().appVersion, "0.1.0");

    // Untrusted input: refused with a message, never a crash.
    EXPECT_FALSE(io::parseRecoverySidecar("").ok());
    EXPECT_FALSE(io::parseRecoverySidecar("not json").ok());
    EXPECT_FALSE(io::parseRecoverySidecar("[]").ok());
    EXPECT_FALSE(io::parseRecoverySidecar(R"({"format":"Other","version":1})").ok());
    EXPECT_FALSE(io::parseRecoverySidecar(R"({"format":"OpenShapeRecovery"})").ok()) << "no version";
    EXPECT_FALSE(io::parseRecoverySidecar(R"({"format":"OpenShapeRecovery","version":1,"title":5})").ok());
    EXPECT_FALSE(io::parseRecoverySidecar(R"({"format":"OpenShapeRecovery","version":1,"savedAt":"noon"})").ok());
    EXPECT_FALSE(io::parseRecoverySidecar(R"({"format":"OpenShapeRecovery","version":1,"title":")" + std::string(600, 'x')
                                          + R"("})")
                     .ok())
        << "overlong title";
    const auto failed = io::parseRecoverySidecar("{");
    EXPECT_EQ(failed.error(), ErrorCode::FileFormatError);
    EXPECT_FALSE(failed.userMessage().empty());

    // Optional fields may be missing; a newer version's extra fields are ignored.
    const auto minimal = io::parseRecoverySidecar(R"({"format":"OpenShapeRecovery","version":7,"future":{"x":1}})");
    ASSERT_TRUE(minimal.ok());
    EXPECT_TRUE(minimal.value().originalPath.empty());
    EXPECT_EQ(minimal.value().savedAtMs, 0);
}

TEST(Recovery, WriteListAndLoadWithSameVolume)
{
    TempDir dir("write");
    const io::RecoveryStore store(dir.path / "recovery"); // created on the first write
    EXPECT_TRUE(store.list().empty()) << "no folder yet";

    const auto document = cubeDocument(20);
    const std::string s = session();
    io::RecoveryInfo info;
    info.originalPath = "D:/parts/cube.openshape";
    info.title = "cube";
    info.appVersion = "0.1.0";
    const auto before = io::unixTimeMs();
    ASSERT_TRUE(store.write(s, *document, info).ok());
    EXPECT_TRUE(store.exists(s));

    const auto entries = store.list();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].session, s);
    EXPECT_TRUE(entries[0].hasSidecar);
    EXPECT_EQ(entries[0].info.originalPath, "D:/parts/cube.openshape");
    EXPECT_EQ(entries[0].info.title, "cube");
    EXPECT_GE(entries[0].info.savedAtMs, before);
    EXPECT_LE(entries[0].info.savedAtMs, io::unixTimeMs());

    // The copy is a normal project file with the same geometry.
    auto loaded = io::loadProject(entries[0].projectFile);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    ASSERT_EQ(loaded.value()->bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(loaded.value()->bodies().front()->shape()), 8000.0, 1e-6);

    // Rewriting replaces it (one copy per session), with no temp files left.
    const auto bigger = cubeDocument(30);
    ASSERT_TRUE(store.write(s, *bigger, info).ok());
    ASSERT_EQ(store.list().size(), 1u);
    auto reloaded = io::loadProject(store.projectFile(s));
    ASSERT_TRUE(reloaded.ok());
    EXPECT_NEAR(geom::volume(reloaded.value()->bodies().front()->shape()), 27000.0, 1e-6);
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(store.directory())) {
        EXPECT_NE(e.path().extension(), ".tmp") << e.path();
        ++files;
    }
    EXPECT_EQ(files, 2) << "the copy and its sidecar";
}

TEST(Recovery, RecoveryCopiesLeaveOutTheGeometryCache)
{
    // The cache is rebuilt on load; leaving it out keeps copies small and quick.
    TempDir dir("small");
    const io::RecoveryStore store(dir.path);
    const auto document = cubeDocument(20);
    const std::string s = session();
    ASSERT_TRUE(store.write(s, *document, {}).ok());
    const auto full = dir.path / "full.openshape";
    ASSERT_TRUE(io::saveProject(*document, full).ok());
    EXPECT_LT(std::filesystem::file_size(store.projectFile(s)), std::filesystem::file_size(full));
}

TEST(Recovery, OrphanedSkipsLiveSessionsAndOurOwn)
{
    TempDir dir("orphans");
    const io::RecoveryStore store(dir.path);
    const auto document = cubeDocument(10);
    const std::string mine = session(), live = session(), crashedA = session(), crashedB = session();
    io::RecoveryInfo older;
    older.title = "older";
    older.savedAtMs = 1000;
    io::RecoveryInfo newer;
    newer.title = "newer";
    newer.savedAtMs = 2000;
    for (const auto& s : {mine, live})
        ASSERT_TRUE(store.write(s, *document, {}).ok());
    ASSERT_TRUE(store.write(crashedA, *document, older).ok());
    ASSERT_TRUE(store.write(crashedB, *document, newer).ok());
    writeText(dir.path / "notes.txt", "unrelated");
    writeText(dir.path / "not-a-session.openshape", "junk");

    EXPECT_EQ(store.list().size(), 4u) << "junk files are ignored";
    const auto isAlive = [&](const std::string& s) { return s == live || s == mine; };
    const auto orphans = store.orphaned(isAlive, mine);
    ASSERT_EQ(orphans.size(), 2u);
    EXPECT_EQ(orphans[0].session, crashedB) << "newest first";
    EXPECT_EQ(orphans[0].info.title, "newer");
    EXPECT_EQ(orphans[1].session, crashedA);
    // Our own session is never offered, even if the liveness check says otherwise.
    EXPECT_EQ(sessionsOf(store.orphaned([](const std::string&) { return false; }, mine)),
              (std::set<std::string>{live, crashedA, crashedB}));
}

TEST(Recovery, CopyWithoutSidecarIsStillOffered)
{
    // A crash between writing the copy and its sidecar must not lose the work.
    TempDir dir("nosidecar");
    const io::RecoveryStore store(dir.path);
    const std::string s = session();
    ASSERT_TRUE(store.write(s, *cubeDocument(10), {}).ok());
    std::filesystem::remove(store.sidecarFile(s));
    const std::string damaged = session();
    ASSERT_TRUE(store.write(damaged, *cubeDocument(10), {}).ok());
    writeText(store.sidecarFile(damaged), "{ damaged");
    const auto entries = store.orphaned([](const std::string&) { return false; }, session());
    ASSERT_EQ(entries.size(), 2u);
    for (const auto& e : entries) {
        EXPECT_FALSE(e.hasSidecar);
        EXPECT_TRUE(e.info.title.empty());
        EXPECT_EQ(e.info.savedAtMs, 0);
    }
}

TEST(Recovery, RemoveAndAdopt)
{
    TempDir dir("adopt");
    const io::RecoveryStore store(dir.path);
    const std::string crashed = session(), mine = session();
    io::RecoveryInfo info;
    info.title = "restored";
    info.originalPath = "C:/p/restored.openshape";
    ASSERT_TRUE(store.write(crashed, *cubeDocument(10), info).ok());
    ASSERT_TRUE(store.write(mine, *cubeDocument(20), {}).ok());

    // Restoring moves the crashed session's copy to ours (replacing ours).
    ASSERT_TRUE(store.adopt(crashed, mine, info).ok());
    EXPECT_FALSE(store.exists(crashed));
    EXPECT_FALSE(std::filesystem::exists(store.sidecarFile(crashed)));
    ASSERT_TRUE(store.exists(mine));
    const auto entries = store.list();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].info.title, "restored");
    EXPECT_EQ(entries[0].info.originalPath, "C:/p/restored.openshape");
    auto loaded = io::loadProject(store.projectFile(mine));
    ASSERT_TRUE(loaded.ok());
    EXPECT_NEAR(geom::volume(loaded.value()->bodies().front()->shape()), 1000.0, 1e-6);

    EXPECT_FALSE(store.adopt(crashed, mine, info).ok()) << "nothing left to adopt";
    EXPECT_FALSE(store.adopt(mine, mine, info).ok());
    EXPECT_FALSE(store.adopt("../x", mine, info).ok());

    ASSERT_TRUE(store.remove(mine).ok());
    EXPECT_TRUE(store.list().empty());
    EXPECT_TRUE(store.remove(mine).ok()) << "removing twice is fine";
    EXPECT_FALSE(store.remove("..").ok());
}

TEST(Recovery, AdoptKeepsTheOriginalFileWhenTheOldSidecarIsHeld)
{
    // A virus scanner or the indexer holds the crashed session's sidecar: it
    // cannot be moved or deleted (on Windows an open file cannot be). The
    // adopted copy must still know the user's file, or a second crash would
    // offer it as "Untitled" and Save would ask for a location.
    TempDir dir("adoptheld");
    const io::RecoveryStore store(dir.path);
    const std::string crashed = session(), mine = session();
    io::RecoveryInfo info;
    info.title = "bracket";
    info.originalPath = "C:/parts/bracket.openshape";
    info.savedAtMs = 1'790'000'000'000;
    ASSERT_TRUE(store.write(crashed, *cubeDocument(10), info).ok());
    {
        std::ifstream held(store.sidecarFile(crashed), std::ios::binary);
        ASSERT_TRUE(held.is_open());
        ASSERT_TRUE(store.adopt(crashed, mine, info).ok());
        EXPECT_FALSE(store.exists(crashed));
    }
    const auto entries = store.list();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].session, mine);
    ASSERT_TRUE(entries[0].hasSidecar);
    EXPECT_EQ(entries[0].info.originalPath, "C:/parts/bracket.openshape");
    EXPECT_EQ(entries[0].info.title, "bracket");
    EXPECT_EQ(entries[0].info.savedAtMs, info.savedAtMs) << "the time of the crashed work";
    // An old sidecar left behind is cleaned up once it is free.
    (void)store.removeLeftovers([](const std::string&) { return false; }, mine);
    EXPECT_FALSE(std::filesystem::exists(store.sidecarFile(crashed)));
    EXPECT_TRUE(std::filesystem::exists(store.sidecarFile(mine)));

    // A copy that had no sidecar (a crash between the two writes) gets one.
    const std::string bare = session(), next = session();
    ASSERT_TRUE(store.write(bare, *cubeDocument(10), {}).ok());
    std::filesystem::remove(store.sidecarFile(bare));
    ASSERT_TRUE(store.adopt(bare, next, {}).ok());
    const auto adopted = store.orphaned([](const std::string&) { return false; }, mine);
    ASSERT_EQ(adopted.size(), 1u);
    EXPECT_EQ(adopted[0].session, next);
    EXPECT_TRUE(adopted[0].hasSidecar);
    EXPECT_TRUE(adopted[0].info.originalPath.empty());
}

TEST(Recovery, LeftoversOfDeadSessionsAreCleanedUp)
{
    TempDir dir("leftovers");
    const io::RecoveryStore store(dir.path);
    std::filesystem::create_directories(dir.path);
    const std::string dead = session(), live = session(), mine = session(), keep = session();
    writeText(dir.path / (dead + ".openshape.tmp"), "half a zip");
    writeText(dir.path / (dead + ".json.tmp"), "{");
    writeText(store.sidecarFile(dead), io::recoverySidecarJson({})); // sidecar without its copy
    writeText(dir.path / (live + ".openshape.tmp"), "being written right now");
    writeText(dir.path / (mine + ".json.tmp"), "ours");
    ASSERT_TRUE(store.write(keep, *cubeDocument(5), {}).ok()); // a dead session's real copy stays
    const auto isAlive = [&](const std::string& s) { return s == live; };
    EXPECT_EQ(store.removeLeftovers(isAlive, mine), 3);
    EXPECT_TRUE(std::filesystem::exists(dir.path / (live + ".openshape.tmp")));
    EXPECT_TRUE(std::filesystem::exists(dir.path / (mine + ".json.tmp")));
    EXPECT_TRUE(store.exists(keep));
    EXPECT_TRUE(std::filesystem::exists(store.sidecarFile(keep)));
    EXPECT_EQ(store.removeLeftovers(isAlive, mine), 0);
}

TEST(Recovery, UnwritableFolderIsAPlainFailure)
{
    TempDir dir("unwritable");
    std::filesystem::create_directories(dir.path);
    writeText(dir.path / "file", "a file where the folder should be");
    const io::RecoveryStore store(dir.path / "file" / "recovery");
    const auto status = store.write(session(), *cubeDocument(5), {});
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error(), ErrorCode::FileWriteError);
    // Shown a few seconds after an ordinary edit: it must not read like a failed Save.
    EXPECT_NE(status.userMessage().find("recovery copy"), std::string::npos) << status.userMessage();
    EXPECT_NE(status.userMessage().find("Your file is not affected"), std::string::npos) << status.userMessage();
    EXPECT_FALSE(status.developerMessage().empty());
    EXPECT_FALSE(store.write("bad name", *cubeDocument(5), {}).ok());

    // The folder exists but the copy cannot be written (a folder in its
    // place): the save's own message ("Unable to save...") is not shown.
    const io::RecoveryStore blocked(dir.path / "blocked");
    const std::string s = session();
    std::filesystem::create_directories(blocked.projectFile(s));
    const auto saveFailed = blocked.write(s, *cubeDocument(5), {});
    ASSERT_FALSE(saveFailed.ok());
    EXPECT_NE(saveFailed.userMessage().find("recovery copy"), std::string::npos) << saveFailed.userMessage();
    EXPECT_EQ(saveFailed.userMessage().find("Unable to save"), std::string::npos) << saveFailed.userMessage();
    EXPECT_FALSE(saveFailed.developerMessage().empty());
}

// ---- Recent files ------------------------------------------------------------------

TEST(RecentFiles, MostRecentFirstWithoutDuplicates)
{
    std::vector<std::string> list;
    list = io::withRecentFile(list, "C:/a.openshape");
    list = io::withRecentFile(list, "C:/b.openshape");
    list = io::withRecentFile(list, "C:/c.openshape");
    EXPECT_EQ(list, (std::vector<std::string>{"C:/c.openshape", "C:/b.openshape", "C:/a.openshape"}));
    list = io::withRecentFile(list, "C:/a.openshape");
    EXPECT_EQ(list, (std::vector<std::string>{"C:/a.openshape", "C:/c.openshape", "C:/b.openshape"}));
    // The same file spelled with backslashes is not a second entry.
    list = io::withRecentFile(list, "C:\\b.openshape");
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list.front(), "C:\\b.openshape");
    EXPECT_EQ(io::withRecentFile(list, ""), list) << "an empty path changes nothing";
#if defined(_WIN32)
    EXPECT_TRUE(io::sameRecentPath("C:/Parts/A.openshape", "c:\\parts\\a.OPENSHAPE"));
#else
    EXPECT_FALSE(io::sameRecentPath("/parts/A.openshape", "/parts/a.openshape"));
#endif
}

TEST(RecentFiles, KeepsTheLastTen)
{
    std::vector<std::string> list;
    for (int i = 0; i < 25; ++i)
        list = io::withRecentFile(list, "/p/" + std::to_string(i) + ".openshape");
    ASSERT_EQ(list.size(), io::kMaxRecentFiles);
    EXPECT_EQ(list.front(), "/p/24.openshape");
    EXPECT_EQ(list.back(), "/p/15.openshape");
    EXPECT_EQ(io::withRecentFile(list, "/p/new.openshape", 3).size(), 3u);
}

TEST(RecentFiles, FilesThatAreGoneDoNotPushExistingOnesOut)
{
    // Ten remembered projects, four of them deleted since: opening two more
    // still leaves every existing one in the menu (not 7 or 8 of them).
    std::set<std::string> gone;
    std::vector<std::string> list;
    for (int i = 0; i < 10; ++i)
        list.insert(list.begin(), "/p/" + std::to_string(i) + ".openshape"); // 9 first
    for (int i : {9, 6, 3, 1})
        gone.insert("/p/" + std::to_string(i) + ".openshape");
    const auto exists = [&](const std::string& p) { return !gone.contains(p); };
    list = io::withRecentFile(list, "/p/10.openshape", io::kMaxRecentFiles, exists);
    list = io::withRecentFile(list, "/p/11.openshape", io::kMaxRecentFiles, exists);
    auto shownOf = [&](const std::vector<std::string>& l) {
        std::vector<std::string> shown;
        for (const auto& p : l)
            if (exists(p))
                shown.push_back(p);
        return shown;
    };
    EXPECT_EQ(shownOf(list), (std::vector<std::string>{"/p/11.openshape", "/p/10.openshape", "/p/8.openshape", "/p/7.openshape",
                                                       "/p/5.openshape", "/p/4.openshape", "/p/2.openshape", "/p/0.openshape"}))
        << "all eight existing ones stay";
    EXPECT_EQ(list.size(), 12u) << "the four missing ones keep their place (a USB stick may come back)";

    // Four more: an existing entry drops out only once ten newer existing ones
    // come before it (and missing ones older than that go with it).
    for (int i = 12; i < 16; ++i)
        list = io::withRecentFile(list, "/p/" + std::to_string(i) + ".openshape", io::kMaxRecentFiles, exists);
    const auto shown = shownOf(list);
    ASSERT_EQ(shown.size(), io::kMaxRecentFiles);
    EXPECT_EQ(shown.front(), "/p/15.openshape");
    EXPECT_EQ(shown.back(), "/p/4.openshape");
    for (const auto& p : list) {
        EXPECT_NE(p, "/p/2.openshape") << "older than the ten shown: dropped";
        EXPECT_NE(p, "/p/0.openshape");
    }

    // Missing entries cannot pile up: at most `limit` of them are kept.
    std::vector<std::string> ghosts;
    for (int i = 0; i < 30; ++i)
        ghosts = io::withRecentFile(ghosts, "/gone/" + std::to_string(i), 10, [](const std::string&) { return false; });
    EXPECT_EQ(ghosts.size(), 10u);
    EXPECT_EQ(ghosts.front(), "/gone/29");
}

TEST(RecentFiles, OnlyFilesThatStillExist)
{
    TempDir dir("recent");
    std::filesystem::create_directories(dir.path);
    const auto a = dir.path / "a.openshape";
    const auto u8 = (dir.path / "\xC3\xBC.openshape").u8string(); // non-ASCII name
    const std::string unicode(u8.begin(), u8.end());
    writeText(a, "x");
    writeText(std::filesystem::path(std::u8string(u8)), "x");
    const auto aU8 = a.u8string();
    const std::string aText(aU8.begin(), aU8.end());
    const std::vector<std::string> list{aText, (dir.path / "gone.openshape").string(), unicode, dir.path.string()};
    EXPECT_EQ(io::existingRecentFiles(list), (std::vector<std::string>{aText, unicode})) << "folders are not files";
}
