#include "geometry/Modeling.h"
#include "io/Export3mf.h"

#include <zip.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>

using namespace os;

namespace {

// Signed volume of a closed triangle mesh (divergence theorem).
double meshVolume(const io::WeldedMesh& m)
{
    double v = 0;
    for (const auto& t : m.triangles)
        v += m.vertices[t[0]].dot(m.vertices[t[1]].cross(m.vertices[t[2]])) / 6.0;
    return v;
}

// Each undirected edge must be used exactly twice, in opposite directions.
bool isClosedManifold(const io::WeldedMesh& m)
{
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (const auto& t : m.triangles)
        for (int i = 0; i < 3; ++i)
            ++directed[{t[i], t[(i + 1) % 3]}];
    for (const auto& [edge, count] : directed) {
        if (count != 1)
            return false;
        if (!directed.contains({edge.second, edge.first}))
            return false;
    }
    return true;
}

std::string readEntry(const std::filesystem::path& zipPath, const char* name)
{
    int err = 0;
    zip_t* z = zip_open(zipPath.string().c_str(), ZIP_RDONLY, &err);
    if (!z)
        return {};
    std::string out;
    zip_stat_t st;
    if (zip_stat(z, name, 0, &st) == 0) {
        zip_file_t* f = zip_fopen(z, name, 0);
        out.resize(st.size);
        zip_fread(f, out.data(), st.size);
        zip_fclose(f);
    }
    zip_close(z);
    return out;
}

} // namespace

TEST(ThreeMf, BoxMeshIsClosedAndExact)
{
    const auto box = geom::makeBox({0, 0, 0}, {60, 40, 20}).value();
    const auto mesh = io::weldedMesh(box);
    EXPECT_EQ(mesh.vertices.size(), 8u);
    EXPECT_EQ(mesh.triangles.size(), 12u);
    EXPECT_TRUE(isClosedManifold(mesh));
    EXPECT_NEAR(meshVolume(mesh), 48000.0, 1e-6) << "outward orientation and exact planar geometry";
}

TEST(ThreeMf, CurvedPartIsClosedAndClose)
{
    const auto plate = geom::makeBox({0, 0, 0}, {60, 30, 5}).value();
    const auto hole = geom::makeCylinder({15, 15, -1}, {0, 0, 1}, 3.0, 7.0).value();
    const auto part = geom::booleanOp(plate, hole, geom::BooleanKind::Subtract).value();
    const auto mesh = io::weldedMesh(part);
    EXPECT_TRUE(isClosedManifold(mesh));
    const double exact = geom::volume(part);
    EXPECT_NEAR(meshVolume(mesh), exact, exact * 1e-3) << "0.01 mm deflection keeps the error tiny";
}

TEST(ThreeMf, PackageStructure)
{
    const auto box = geom::makeBox({0, 0, 0}, {10, 10, 10}).value();
    const auto path = std::filesystem::temp_directory_path() / "openshape_test.3mf";
    std::filesystem::remove(path);
    ASSERT_TRUE(io::export3mf({{"Cube & Co", box}}, path).ok());
    const std::string types = readEntry(path, "[Content_Types].xml");
    const std::string rels = readEntry(path, "_rels/.rels");
    const std::string model = readEntry(path, "3D/3dmodel.model");
    EXPECT_NE(types.find("3dmanufacturing-3dmodel+xml"), std::string::npos);
    EXPECT_NE(rels.find("/3D/3dmodel.model"), std::string::npos);
    EXPECT_NE(model.find("unit=\"millimeter\""), std::string::npos);
    EXPECT_NE(model.find("name=\"Cube &amp; Co\""), std::string::npos);
    EXPECT_NE(model.find("<item objectid=\"1\"/>"), std::string::npos);
    EXPECT_FALSE(io::export3mf({}, path).ok());
}
