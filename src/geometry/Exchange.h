// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <filesystem>
#include <string>
#include <vector>

// Neutral-format import/export. Imported files are untrusted input: size is
// checked before handing them to the kernel, and every failure is reported
// as a Result rather than an exception.
namespace os::geom {

struct NamedShape {
    std::string name;
    Shape shape;
};

// The unit a STEP file is written in (coordinates are converted; readers
// convert back, so the part keeps its size).
enum class StepUnit { Millimeter, Inch, Meter };

struct StepWriteOptions {
    StepUnit unit = StepUnit::Millimeter;
    // Write each shape's boundary as surfaces (a shell based surface model)
    // instead of solids, as some CAD programs do. Importers must close them
    // into solids again (tests use it for that path).
    bool surfacesOnly = false;
};

// Writes all shapes into one STEP AP214 file, one product per shape named
// after it (other CAD programs show those names as part names).
Status exportStep(const std::vector<NamedShape>& shapes, const std::filesystem::path& path,
                  const StepWriteOptions& options = {});

// Reads the closed solids of a STEP file, in millimeters whatever unit the
// file uses (inches, meters, ...), placed as in the file (assemblies are
// flattened with their placements). Each solid is one NamedShape named after
// its STEP product ("Bracket"; "Bracket 1", "Bracket 2" when a product has
// several solids); the name is empty when the file gives none. Closed shells
// (surface models) become solids; open surfaces, loose faces, curves and
// invalid solids are skipped and reported in plain language as warnings of
// the Result. Fails with a plain message when nothing usable is left.
Result<std::vector<NamedShape>> importStep(const std::filesystem::path& path);

struct StlOptions {
    bool binary = true;
    double linearDeflection = 0.01; // mm; fine enough for FDM/resin printing
    double angularDeflection = 0.2; // radians
};
Status exportStl(const std::vector<NamedShape>& shapes, const std::filesystem::path& path, const StlOptions& options = {});

// Upper bound for neutral files we are willing to parse.
inline constexpr std::uintmax_t kMaxImportFileBytes = 512ull * 1024 * 1024;
// At most this many solids are taken from one file (a larger assembly is
// refused rather than freezing the app with tens of thousands of bodies).
inline constexpr std::size_t kMaxImportSolids = 2000;

} // namespace os::geom
