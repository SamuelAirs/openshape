// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// thumbnail.png in project files, and reading just that (the start screen).

#include "TestHelpers.h"

#include "core/Uuid.h"
#include "io/ProjectFile.h"

#include <zip.h>

#include <filesystem>
#include <fstream>

using namespace os;
using namespace os::test;

namespace {

std::filesystem::path temp(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_thumb_" + Uuid::generate().toString() + "_" + name);
}

std::vector<unsigned char> fakePng(std::size_t size)
{
    std::vector<unsigned char> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (std::size_t i = png.size(); i < size; ++i)
        png.push_back(static_cast<unsigned char>(i * 7));
    return png;
}

void writeZip(const std::filesystem::path& p, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::filesystem::remove(p);
    int err = 0;
    zip_t* z = zip_open(p.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    ASSERT_NE(z, nullptr);
    for (const auto& [name, data] : entries) {
        zip_source_t* s = zip_source_buffer(z, data.data(), data.size(), 0);
        ASSERT_GE(zip_file_add(z, name.c_str(), s, ZIP_FL_OVERWRITE), 0);
    }
    ASSERT_EQ(zip_close(z), 0);
}

std::unique_ptr<doc::Document> boxDocument()
{
    auto d = std::make_unique<doc::Document>();
    auto body = std::make_unique<doc::Body>();
    body->insertFeature(boxFeature(20, 20, 20), 0);
    d->addBody(std::move(body));
    return d;
}

} // namespace

TEST(ProjectThumbnail, SavedAndReadBackAlone)
{
    const auto png = fakePng(5000);
    io::SaveOptions options;
    options.thumbnailPng = png;
    const auto path = temp("with.openshape");
    ASSERT_TRUE(io::saveProject(*boxDocument(), path, options).ok());
    auto read = io::readProjectThumbnail(path);
    ASSERT_TRUE(read.ok()) << read.developerMessage();
    EXPECT_EQ(read.value(), png);
    // The project itself still opens.
    EXPECT_TRUE(io::loadProject(path).ok());

    // Recovery copies carry one too when given (and none without).
    const auto none = temp("without.openshape");
    ASSERT_TRUE(io::saveProject(*boxDocument(), none).ok());
    auto missing = io::readProjectThumbnail(none);
    EXPECT_FALSE(missing.ok());
    EXPECT_EQ(missing.error(), ErrorCode::FileFormatError);
    std::filesystem::remove(path);
    std::filesystem::remove(none);
}

TEST(ProjectThumbnail, NonAsciiPathsWork)
{
    const auto png = fakePng(100);
    io::SaveOptions options;
    options.thumbnailPng = png;
    const auto path = std::filesystem::temp_directory_path()
                    / std::filesystem::path(u8"openshape_thumb_été_模型_" + std::u8string(u8"x.openshape"));
    ASSERT_TRUE(io::saveProject(*boxDocument(), path, options).ok());
    auto read = io::readProjectThumbnail(path);
    ASSERT_TRUE(read.ok()) << read.developerMessage();
    EXPECT_EQ(read.value(), png);
    std::filesystem::remove(path);
}

TEST(ProjectThumbnail, UntrustedFilesAreRefused)
{
    EXPECT_EQ(io::readProjectThumbnail(temp("missing.openshape")).error(), ErrorCode::FileNotFound);

    const auto notZip = temp("notzip.openshape");
    {
        std::ofstream out(notZip, std::ios::binary);
        out << "definitely not a zip file";
    }
    EXPECT_EQ(io::readProjectThumbnail(notZip).error(), ErrorCode::FileFormatError);

    const auto notPng = temp("notpng.openshape");
    writeZip(notPng, {{"document.json", "{}"}, {"thumbnail.png", "GIF89a this is not a png"}});
    EXPECT_EQ(io::readProjectThumbnail(notPng).error(), ErrorCode::FileFormatError);

    const auto tiny = temp("tiny.openshape");
    writeZip(tiny, {{"thumbnail.png", "\x89PN"}});
    EXPECT_EQ(io::readProjectThumbnail(tiny).error(), ErrorCode::FileFormatError);

    const auto huge = temp("huge.openshape");
    const auto big = fakePng(std::size_t(io::kMaxThumbnailBytes) + 1);
    writeZip(huge, {{"thumbnail.png", std::string(big.begin(), big.end())}});
    EXPECT_EQ(io::readProjectThumbnail(huge).error(), ErrorCode::FileFormatError);
    for (const auto& p : {notZip, notPng, tiny, huge})
        std::filesystem::remove(p);
}
