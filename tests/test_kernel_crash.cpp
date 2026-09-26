// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Kernel crashes become failures with a message. The stress test found a
// valid part (two filleted and chamfered blocks in one body, with blend
// corners that have degenerate edges) whose mirror or rotated copy made
// OpenCASCADE's General Fuse dereference a null curve inside its solid
// classifier (Extrema_ExtCC via BRepClass3d_BndBoxTreeSelectorLine) and end
// the process. With OCCT's signal handlers installed the fuse reports a
// failed build instead, and the kernel keeps working afterwards.
#include "geometry/Modeling.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace os;

namespace {

geom::Shape loadCrashPart()
{
    std::ifstream in(std::string(OPENSHAPE_TEST_DATA_DIR) + "/fuse-crash-fillet-corners.brep", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    auto shape = geom::fromBrepString(text.str());
    EXPECT_TRUE(shape.ok()) << shape.developerMessage();
    return shape.ok() ? shape.value() : geom::Shape();
}

} // namespace

TEST(KernelCrash, MirrorThatCrashedTheFuseFailsWithAMessage)
{
    const geom::Shape part = loadCrashPart();
    ASSERT_FALSE(part.isNull());
    ASSERT_EQ(part.faceCount(), 26);
    ASSERT_EQ(part.solidCount(), 2);
    ASSERT_TRUE(geom::isValid(part));
    const double volume = geom::volume(part);

    // Mirrored across its top face (z = 22.279 is the top; the plane passes
    // through the part's bounding-box corner, as the Mirror tool's "side" does).
    auto mirrored = geom::mirrorJoined(part, {16.709500291471734, -1.8562408188618207, 11.137240818861827}, {0, 0, 1});
    if (mirrored) {
        EXPECT_TRUE(geom::isValid(mirrored.value()));
        EXPECT_GT(geom::volume(mirrored.value()), volume);
    } else {
        EXPECT_FALSE(mirrored.userMessage().empty());
    }

    // The same through a rotated copy (a circular pattern).
    geom::RigidMotion turn;
    turn.center = {16.709500291471734, -1.8562408188618207, 11.137240818861827};
    turn.axis = {1, 0, 0};
    turn.angle = kPi;
    auto repeated = geom::repeatJoined(part, {turn});
    if (!repeated) {
        EXPECT_FALSE(repeated.userMessage().empty());
    }

    // The kernel still works: a plain boolean on the same part.
    auto box = geom::makeBox({-3, -3, 5}, {5, 5, 5});
    ASSERT_TRUE(box.ok());
    auto cut = geom::booleanOp(part, box.value(), geom::BooleanKind::Subtract);
    ASSERT_TRUE(cut.ok()) << cut.developerMessage();
    EXPECT_LT(geom::volume(cut.value()), volume);
}
