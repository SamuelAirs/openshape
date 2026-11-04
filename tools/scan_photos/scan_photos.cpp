// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Runs Scan (docs/SCAN.md) on real photos, for developers: a folder of binary
// PPM photos (scripts/dev/scan_photos.py makes them from JPEGs, with EXIF
// orientation applied and the 35 mm focal length listed) and an optional
// photos.txt ("name.ppm f35 [x0 y0 x1 y1 x2 y2 x3 y3]" per line; corners left
// out are found by measure::detectPaper). Writes the mesh as a binary STL,
// each photo's mask as a PGM (white object, black background, grey unknown),
// and prints what became of each photo.
//
//   scan_photos <folder> [--letter] [--out scan.stl] [--height mm]

#include "measure/PaperDetect.h"
#include "measure/Scan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace os;
using namespace os::measure;

namespace {

bool readPpm(const std::filesystem::path& path, RgbImage& image)
{
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int maxValue = 0;
    if (!(in >> magic) || magic != "P6")
        return false;
    auto next = [&](int& v) {
        while (in >> std::ws && in.peek() == '#')
            in.ignore(1 << 20, '\n');
        return bool(in >> v);
    };
    if (!next(image.width) || !next(image.height) || !next(maxValue) || maxValue != 255)
        return false;
    in.get();
    image.rgb.resize(std::size_t(image.width) * image.height * 3);
    in.read(reinterpret_cast<char*>(image.rgb.data()), std::streamsize(image.rgb.size()));
    return bool(in);
}

void writePgm(const std::filesystem::path& path, const std::vector<std::uint8_t>& data, int w, int h)
{
    std::ofstream out(path, std::ios::binary);
    out << "P5\n" << w << " " << h << "\n255\n";
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

void writeStl(const std::filesystem::path& path, const ScanMesh& mesh)
{
    std::ofstream out(path, std::ios::binary);
    char header[80] = "FoldForm scan";
    out.write(header, 80);
    const std::uint32_t n = std::uint32_t(mesh.triangles.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& t : mesh.triangles) {
        const Vec3 a = mesh.vertices[t[0]], b = mesh.vertices[t[1]], c = mesh.vertices[t[2]];
        const Vec3 nn = (b - a).cross(c - a).normalized();
        const float data[12] = {float(nn.x), float(nn.y), float(nn.z), float(a.x), float(a.y), float(a.z),
                                float(b.x),  float(b.y),  float(b.z),  float(c.x), float(c.y), float(c.z)};
        out.write(reinterpret_cast<const char*>(data), 48);
        const std::uint16_t attr = 0;
        out.write(reinterpret_cast<const char*>(&attr), 2);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: scan_photos <folder> [--letter] [--out scan.stl] [--height mm]\n");
        return 2;
    }
    const std::filesystem::path folder = argv[1];
    std::filesystem::path outPath = folder / "scan.stl";
    ScanOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--letter")
            options.paper = letterReference();
        else if (arg == "--out" && i + 1 < argc)
            outPath = argv[++i];
        else if (arg == "--height" && i + 1 < argc)
            options.heightMm = std::atof(argv[++i]);
    }
    struct Listed {
        double focal35mm = 0;
        bool hasCorners = false;
        std::array<Vec2, 4> corners{};
    };
    std::map<std::string, Listed> listed;
    std::ifstream list(folder / "photos.txt");
    for (std::string line; std::getline(list, line);) {
        std::istringstream words(line);
        std::string name;
        Listed l;
        if (!(words >> name >> l.focal35mm))
            continue;
        double v[8];
        int count = 0;
        while (count < 8 && words >> v[count])
            ++count;
        if (count == 8) {
            l.hasCorners = true;
            for (int k = 0; k < 4; ++k)
                l.corners[k] = {v[2 * k], v[2 * k + 1]};
        }
        listed[name] = l;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        if (entry.path().extension() == ".ppm")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    std::vector<RgbImage> images(files.size());
    std::vector<ScanPhoto> photos;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (!readPpm(files[i], images[i])) {
            std::printf("%s: not a binary PPM, skipped\n", files[i].filename().string().c_str());
            continue;
        }
        ScanPhoto p;
        p.pixels = images[i].rgb.data();
        p.width = images[i].width;
        p.height = images[i].height;
        p.stride = p.width * 3;
        p.bytesPerPixel = 3;
        const auto it = listed.find(files[i].filename().string());
        if (it != listed.end()) {
            p.focal35mm = it->second.focal35mm;
            if (it->second.hasCorners)
                p.paperCorners = it->second.corners;
        }
        if (it == listed.end() || !it->second.hasCorners) {
            const auto found = detectPaper(images[i]);
            if (found)
                p.paperCorners = *found;
            std::printf("%s: paper %s\n", files[i].filename().string().c_str(), found ? "found" : "NOT found");
        }
        photos.push_back(p);
    }

    const auto start = std::chrono::steady_clock::now();
    auto calibration = calibrate(photos, options);
    if (!calibration) {
        std::printf("calibration failed: %s (%s)\n", calibration.userMessage().c_str(), calibration.developerMessage().c_str());
        return 1;
    }
    for (std::size_t i = 0; i < calibration->photos.size(); ++i) {
        const ScanPhotoReport& r = calibration->photos[i];
        std::printf("photo %zu: %s focal %.1f mm%s, corners %.2f px, labelling %d (%s, margin %.3f), az %.0f el %.0f dist %.0f, "
                    "object %.1f %%%s%s\n",
                    i, r.used ? "used" : "LEFT OUT", r.focal35mm, r.focalFromExif ? "" : " (est.)", r.reprojectionErrorPx,
                    r.firstCorner, r.reversed ? "reversed" : "forward", r.labellingMargin, r.azimuthDeg, r.elevationDeg,
                    r.distanceMm, 100 * r.objectFraction, r.problem.empty() ? "" : " - ", r.problem.c_str());
    }
    for (const ScanView& v : calibration->views)
        writePgm(folder / ("mask_" + std::to_string(v.photoIndex) + ".pgm"), v.mask, v.camera.width, v.camera.height);
    auto hull = carve(calibration->views, options);
    if (!hull) {
        std::printf("carving failed: %s\n", hull.userMessage().c_str());
        return 1;
    }
    auto surface = extractSurface(hull.value(), options);
    if (!surface) {
        std::printf("surface failed: %s\n", surface.userMessage().c_str());
        return 1;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto agreement = silhouetteAgreement(calibration->views, hull.value());
    for (std::size_t u = 0; u < agreement.size(); ++u)
        std::printf("photo %d: the shape covers %.1f %% of its outline\n", calibration->views[u].photoIndex, 100 * agreement[u]);
    for (const auto* warnings : {&calibration.warnings(), &hull.warnings(), &surface.warnings()})
        for (const std::string& w : *warnings)
            std::printf("warning: %s\n", w.c_str());
    const ScanSurface& s = surface.value();
    std::printf("scan: %d triangles, %.0f mm3, box %.1f..%.1f x %.1f..%.1f x %.1f..%.1f mm, %.0f ms; focal %.2f mm%s\n",
                s.triangleCount, s.volumeMm3, s.boundsMin.x, s.boundsMax.x, s.boundsMin.y, s.boundsMax.y, s.boundsMin.z,
                s.boundsMax.z, ms, calibration->focal35mm, calibration->focalEstimated ? " (estimated)" : "");
    writeStl(outPath, s.mesh);
    std::printf("wrote %s\n", outPath.string().c_str());
    return 0;
}
