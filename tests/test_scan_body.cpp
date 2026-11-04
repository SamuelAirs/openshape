// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A scanned mesh as a body (AppController::addMeshBody's path, driven here
// through the interaction controller as the app drives it): the mesh the
// Scan surface stage makes becomes an Imported body, one undo step, kept by
// the project file; push/pull works on it like on any body.

#include "commands/Command.h"
#include "core/Uuid.h"
#include "document/Document.h"
#include "geometry/MeshSolid.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"
#include "measure/MeshTools.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>

using namespace os;

namespace {

// What the Scan surface stage makes of a rounded block standing on the
// paper: 40 x 30 x 20 with 4 mm round edges (a signed field), ~3000 triangles.
measure::ScanMesh scannedBlock()
{
    measure::ScanGrid g;
    g.voxel = 0.5;
    g.nx = 100;
    g.ny = 80;
    g.nz = 50;
    g.origin = {-25, -20, 0};
    g.field.resize(std::size_t(g.nx) * g.ny * g.nz);
    const Vec3 half{20, 15, 10}, center{0, 0, 10};
    const double r = 4;
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i) {
                const Vec3 p = g.sampleCenter(i, j, k) - center;
                const Vec3 q{std::abs(p.x) - (half.x - r), std::abs(p.y) - (half.y - r), std::abs(p.z) - (half.z - r)};
                const Vec3 outside{std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0)};
                const double d = outside.length() + std::min(std::max({q.x, q.y, q.z}), 0.0) - r;
                g.field[g.index(i, j, k)] = float(-d);
            }
    std::vector<char> bottom;
    measure::ScanMesh mesh = measure::surfaceNets(g, true, &bottom);
    measure::taubinSmooth(mesh, 6, &bottom);
    measure::decimate(mesh, 3000, &bottom);
    return mesh;
}

} // namespace

TEST(ScanBody, ScannedMeshBecomesABodyAndIsSaved)
{
    const measure::ScanMesh mesh = scannedBlock();
    const measure::MeshReport report = measure::inspectMesh(mesh);
    ASSERT_TRUE(report.oriented());
    ASSERT_LE(report.triangles, 3000);

    const auto start = std::chrono::steady_clock::now();
    auto solid = geom::solidFromMesh(mesh.vertices, mesh.triangles);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ASSERT_TRUE(solid) << solid.developerMessage();
    std::printf("[scan] a %d-triangle scan as a solid: %.0f ms, %d faces (the flat bottom merged)\n", report.triangles, ms,
                solid->faceCount());
    EXPECT_NEAR(geom::volume(solid.value()), report.volume, 1e-6 * report.volume);
    EXPECT_LT(solid->faceCount(), report.triangles); // the bottom is one face
    const geom::BoundingBox box = geom::boundingBox(solid.value());
    EXPECT_NEAR(box.min.z, 0.0, 1e-6);
    EXPECT_NEAR(box.max.z, 20.0, 0.6);
    EXPECT_NEAR(box.max.x - box.min.x, 40.0, 0.8);

    doc::Document document;
    cmd::UndoStack stack;
    interact::InteractionController controller{document, stack};
    controller.setViewportSize({1200, 800});
    ASSERT_TRUE(controller.importBodies({geom::NamedShape{"Scan 1", solid.value()}}, "Scan").ok());
    ASSERT_EQ(document.bodies().size(), 1u);
    EXPECT_EQ(document.bodies()[0]->name(), "Scan 1");
    const double volume = geom::volume(document.bodies()[0]->shape());
    EXPECT_NEAR(volume, report.volume, 1e-6 * report.volume);
    EXPECT_TRUE(controller.undo());
    EXPECT_TRUE(document.bodies().empty());
    EXPECT_TRUE(controller.redo());
    ASSERT_EQ(document.bodies().size(), 1u);

    const auto path = std::filesystem::temp_directory_path() / ("openshape_scan_" + Uuid::generate().toString() + ".openshape");
    ASSERT_TRUE(io::saveProject(document, path).ok());
    auto loaded = io::loadProject(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    ASSERT_EQ(loaded.value()->bodies().size(), 1u);
    EXPECT_EQ(loaded.value()->bodies()[0]->name(), "Scan 1");
    EXPECT_NEAR(geom::volume(loaded.value()->bodies()[0]->shape()), volume, 1e-6 * volume);
}

TEST(ScanBody, BrokenMeshesAreRefusedPlainly)
{
    measure::ScanMesh mesh = scannedBlock();
    mesh.triangles.pop_back();
    auto solid = geom::solidFromMesh(mesh.vertices, mesh.triangles);
    ASSERT_FALSE(solid);
    EXPECT_NE(solid.userMessage().find("not closed"), std::string::npos) << solid.userMessage();
}
