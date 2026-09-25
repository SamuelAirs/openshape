#include "geometry/Modeling.h"
#include "geometry/Profiles.h"

#include <gtest/gtest.h>

using namespace os;
using namespace os::geom;

namespace {

PlanarCurve seg(double x0, double y0, double x1, double y1)
{
    PlanarCurve c;
    c.kind = PlanarCurve::Kind::Segment;
    c.start = {x0, y0, 0};
    c.end = {x1, y1, 0};
    return c;
}

PlanarCurve circle(double cx, double cy, double r)
{
    PlanarCurve c;
    c.kind = PlanarCurve::Kind::Circle;
    c.center = {cx, cy, 0};
    c.radius = r;
    return c;
}

std::vector<PlanarCurve> rectangle(double x0, double y0, double x1, double y1)
{
    return {seg(x0, y0, x1, y0), seg(x1, y0, x1, y1), seg(x1, y1, x0, y1), seg(x0, y1, x0, y0)};
}

std::vector<Region> regions(const std::vector<PlanarCurve>& curves, const PlaneFrame& plane = {})
{
    auto r = findRegions(plane, curves);
    EXPECT_TRUE(r.ok()) << r.developerMessage();
    return r.ok() ? r.value() : std::vector<Region>{};
}

} // namespace

TEST(Profiles, RectangleIsOneRegion)
{
    const auto r = regions(rectangle(0, 0, 60, 30));
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NEAR(r[0].area, 1800.0, 1e-6);
    EXPECT_NEAR(r[0].centroid.x, 30.0, 1e-6);
    EXPECT_NEAR(r[0].centroid.y, 15.0, 1e-6);
    EXPECT_TRUE(regionContains(r[0].face, r[0].interiorPoint));
}

TEST(Profiles, CircleInsideRectangleGivesTwoRegions)
{
    auto curves = rectangle(0, 0, 60, 30);
    curves.push_back(circle(15, 15, 3));
    const auto r = regions(curves);
    ASSERT_EQ(r.size(), 2u);
    // Sorted by area: plate-with-hole first, then the disc.
    EXPECT_NEAR(r[0].area, 1800.0 - kPi * 9.0, 1e-6);
    EXPECT_NEAR(r[1].area, kPi * 9.0, 1e-6);
    EXPECT_FALSE(regionContains(r[0].face, {15, 15, 0}));
    EXPECT_TRUE(regionContains(r[1].face, {15, 15, 0}));
    EXPECT_TRUE(regionContains(r[0].face, {40, 15, 0}));
}

TEST(Profiles, OpenPolylineHasNoRegion)
{
    EXPECT_TRUE(regions({seg(0, 0, 10, 0), seg(10, 0, 10, 10)}).empty());
    EXPECT_TRUE(regions({}).empty());
}

TEST(Profiles, CrossingLinesEncloseCenterSquare)
{
    // A "#" of four long lines encloses the 10x10 square in the middle.
    const auto r = regions({seg(-5, 0, 15, 0), seg(-5, 10, 15, 10), seg(0, -5, 0, 15), seg(10, -5, 10, 15)});
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NEAR(r[0].area, 100.0, 1e-6);
}

TEST(Profiles, DanglingLineDoesNotBreakRegion)
{
    auto curves = rectangle(0, 0, 20, 20);
    curves.push_back(seg(5, 5, 12, 9)); // floating inside
    const auto r = regions(curves);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NEAR(r[0].area, 400.0, 1e-6);
    auto solid = extrudeFaces({r[0].face}, {0, 0, 5});
    ASSERT_TRUE(solid.ok()) << solid.developerMessage();
    EXPECT_NEAR(volume(solid.value()), 2000.0, 1e-6);
}

TEST(Profiles, TwoSeparateCircles)
{
    const auto r = regions({circle(0, 0, 3), circle(20, 0, 3)});
    ASSERT_EQ(r.size(), 2u);
    EXPECT_NEAR(r[0].area, kPi * 9, 1e-6);
    EXPECT_NEAR(r[1].area, kPi * 9, 1e-6);
}

TEST(Profiles, WorksOnTiltedPlanes)
{
    PlaneFrame plane;
    plane.origin = {5, 5, 5};
    plane.xAxis = {0, 1, 0};
    plane.yAxis = {0, 0, 1};
    auto toWorld = [&](double u, double v) { return plane.origin + plane.xAxis * u + plane.yAxis * v; };
    std::vector<PlanarCurve> curves;
    const double pts[4][2] = {{0, 0}, {8, 0}, {8, 4}, {0, 4}};
    for (int i = 0; i < 4; ++i) {
        PlanarCurve c;
        c.start = toWorld(pts[i][0], pts[i][1]);
        c.end = toWorld(pts[(i + 1) % 4][0], pts[(i + 1) % 4][1]);
        curves.push_back(c);
    }
    const auto r = regions(curves, plane);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NEAR(r[0].area, 32.0, 1e-6);
    EXPECT_NEAR(r[0].centroid.x, 5.0, 1e-6);
}

TEST(Profiles, ExtrudeRectangleAndPlateWithHole)
{
    auto curves = rectangle(0, 0, 60, 30);
    curves.push_back(circle(15, 15, 3));
    const auto r = regions(curves);
    ASSERT_EQ(r.size(), 2u);
    auto plate = extrudeFaces({r[0].face}, {0, 0, 5});
    ASSERT_TRUE(plate.ok()) << plate.developerMessage();
    EXPECT_NEAR(volume(plate.value()), (1800.0 - kPi * 9.0) * 5.0, 1e-4);
    // Extruding both regions together fuses into the full block.
    auto block = extrudeFaces({r[0].face, r[1].face}, {0, 0, 5});
    ASSERT_TRUE(block.ok()) << block.developerMessage();
    EXPECT_NEAR(volume(block.value()), 9000.0, 1e-4);
    // Negative direction.
    auto down = extrudeFaces({r[1].face}, {0, 0, -7});
    ASSERT_TRUE(down.ok());
    EXPECT_NEAR(volume(down.value()), kPi * 9.0 * 7.0, 1e-4);
    EXPECT_NEAR(boundingBox(down.value()).min.z, -7.0, 1e-6);
}
