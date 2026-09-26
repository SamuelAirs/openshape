// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Contact shadows: which part of a body rests on the ground
// (interaction/ContactShadow).

#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "interaction/ContactShadow.h"

#include <gtest/gtest.h>

using namespace os;
using namespace os::interact;

namespace {

ContactFootprint footprintOf(const geom::Shape& shape)
{
    return contactFootprint(geom::tessellate(shape));
}

} // namespace

TEST(ContactShadow, BoxRestingOnTheGround)
{
    const auto f = footprintOf(geom::makeBox({-10, -5, 0}, {20, 10, 7}).value());
    ASSERT_FALSE(f.empty());
    EXPECT_EQ(f.indices.size() % 3, 0u);
    EXPECT_NEAR(f.area, 200.0, 1e-6) << "exactly the bottom face";
    EXPECT_NEAR(f.height, 0.0, 1e-12);
    EXPECT_NEAR(f.size, 20.0, 1e-9);
    EXPECT_NEAR(f.strength(), 0.22, 1e-12);
    EXPECT_NEAR(f.blur(), 0.08 * 20, 1e-12);
}

TEST(ContactShadow, LShapeCastsAnL)
{
    // An L lying flat: 30 x 30 minus a 20 x 20 corner.
    const auto plate = geom::makeBox({0, 0, 0}, {30, 30, 4}).value();
    const auto corner = geom::makeBox({10, 10, -1}, {25, 25, 6}).value();
    const auto l = geom::booleanOp(plate, corner, geom::BooleanKind::Subtract);
    ASSERT_TRUE(l.ok());
    const auto f = footprintOf(l.value());
    EXPECT_NEAR(f.area, 30 * 30 - 20 * 20, 1e-6) << "the L, not its bounding square";
}

TEST(ContactShadow, FadesAsTheBodyRisesAndNoneBelowTheGround)
{
    const auto lifted = footprintOf(geom::makeBox({0, 0, 4}, {20, 20, 5}).value());
    ASSERT_FALSE(lifted.empty());
    EXPECT_NEAR(lifted.height, 4.0, 1e-9);
    EXPECT_NEAR(lifted.strength(), 0.22 * (1 - 4.0 / 10.0), 1e-12);
    EXPECT_GT(lifted.blur(), 0.08 * 20);
    EXPECT_TRUE(footprintOf(geom::makeBox({0, 0, 11}, {20, 20, 5}).value()).empty()) << "floating well above";
    EXPECT_TRUE(footprintOf(geom::makeBox({0, 0, -2}, {20, 20, 5}).value()).empty()) << "through the ground";
    // A cylinder lying on its side touches along a line: no face to rest on.
    const auto rod = geom::makeCylinder({0, 0, 5}, {1, 0, 0}, 5, 40);
    ASSERT_TRUE(rod.ok());
    EXPECT_TRUE(footprintOf(rod.value()).empty());
}
