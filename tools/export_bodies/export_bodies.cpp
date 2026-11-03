// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Writes each visible body of a project as its own binary STL, for renders
// in other programs (scripts/dev/marketing_blender.py lights and renders them
// in Blender), with the bundled fonts so text rebuilds. The files are named "<index> <body name>.stl" in the project's
// body order, so a script can give each body its own material.
//
//   cmake --build build/msys2-ucrt64 --target export_bodies
//   build/msys2-ucrt64/bin/export_bodies.exe "resources/samples/Lamp shade.openshape" out/lamp-shade
#include "document/Feature.h"
#include "geometry/Exchange.h"
#include "geometry/Text.h"
#include "io/ProjectFile.h"
#include "document/Body.h"
#include "document/Document.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace os;

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string text(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

// A body name as a file name: characters Windows refuses become '-'.
std::string fileName(int index, const std::string& name)
{
    std::string cleaned;
    for (char c : name)
        cleaned += std::string("<>:\"/\\|?*").find(c) == std::string::npos && static_cast<unsigned char>(c) >= 32 ? c : '-';
    char prefix[16];
    std::snprintf(prefix, sizeof prefix, "%02d ", index);
    return prefix + (cleaned.empty() ? std::string("Body") : cleaned) + ".stl";
}

// The bundled fonts, read from the source tree (the app reads them from its
// resources), so that text features rebuild.
void registerFonts()
{
    const std::pair<const char*, const char*> fonts[] = {
        {doc::kTextFontRegular, "NotoSans-Regular.ttf"},
        {doc::kTextFontBold, "NotoSans-Bold.ttf"},
    };
    for (const auto& [id, file] : fonts) {
        std::ifstream in(std::filesystem::path(OPENSHAPE_FONTS_DIR) / file, std::ios::binary);
        std::ostringstream bytes;
        bytes << in.rdbuf();
        if (!in || !geom::registerFont(id, bytes.str()))
            std::fprintf(stderr, "the font %s could not be read from %s\n", id, OPENSHAPE_FONTS_DIR);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: export_bodies <project.openshape> <output folder>\n");
        return 2;
    }
    const std::filesystem::path project = pathOf(argv[1]);
    const std::filesystem::path out = pathOf(argv[2]);
    registerFonts();
    const auto document = io::loadProject(project);
    if (!document) {
        std::fprintf(stderr, "%s: %s\n", argv[1], document.userMessage().c_str());
        return 1;
    }
    std::filesystem::create_directories(out);
    // Fine enough that turned and lofted walls stay smooth in a close-up still.
    geom::StlOptions options;
    options.linearDeflection = 0.01;
    options.angularDeflection = 0.08;
    int written = 0;
    int index = 0;
    for (const auto& body : document.value()->bodies()) {
        ++index;
        if (!body->isVisible() || body->shape().isNull())
            continue;
        const std::filesystem::path file = out / pathOf(fileName(index, body->name()));
        if (const Status status = geom::exportStl({{body->name(), body->shape()}}, file, options); !status) {
            std::fprintf(stderr, "%s: %s\n", body->name().c_str(), status.userMessage().c_str());
            return 1;
        }
        std::printf("%s\n", text(file).c_str());
        ++written;
    }
    if (written == 0) {
        std::fprintf(stderr, "%s has no visible body\n", argv[1]);
        return 1;
    }
    return 0;
}
