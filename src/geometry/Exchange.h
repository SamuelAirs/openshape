// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <filesystem>
#include <vector>

// Neutral-format import/export. Imported files are untrusted input: size is
// checked before handing them to the kernel, and every failure is reported
// as a Result rather than an exception.
namespace os::geom {

struct NamedShape {
    std::string name;
    Shape shape;
};

// Writes all shapes into one STEP AP214 file (units: millimeters).
Status exportStep(const std::vector<NamedShape>& shapes, const std::filesystem::path& path);
Result<std::vector<NamedShape>> importStep(const std::filesystem::path& path);

struct StlOptions {
    bool binary = true;
    double linearDeflection = 0.01; // mm; fine enough for FDM/resin printing
    double angularDeflection = 0.2; // radians
};
Status exportStl(const std::vector<NamedShape>& shapes, const std::filesystem::path& path, const StlOptions& options = {});

// Upper bound for neutral files we are willing to parse.
inline constexpr std::uintmax_t kMaxImportFileBytes = 512ull * 1024 * 1024;

} // namespace os::geom
