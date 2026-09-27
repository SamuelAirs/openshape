// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The viewport's studio lighting (core/Lighting.h): a box turned any way
// around the vertical and looked at from every direction shows its faces in
// clearly different shades, the top lightest and the underside darkest.

#include "core/Lighting.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace os;

namespace {

constexpr double kDegree = kPi / 180;

struct Face {
    Vec3 normal;
    int kind; // 1 top, -1 bottom, 0 side
};

// A box turned by `turn` around Z: its top, bottom and four sides.
std::vector<Face> box(double turn)
{
    std::vector<Face> faces{{{0, 0, 1}, 1}, {{0, 0, -1}, -1}};
    for (int k = 0; k < 4; ++k) {
        const double a = turn + k * kPi / 2;
        faces.push_back({{std::cos(a), std::sin(a), 0}, 0});
    }
    return faces;
}

bool adjacent(const Face& a, const Face& b)
{
    return std::abs(a.normal.dot(b.normal)) < 1e-9; // perpendicular: they share an edge
}

} // namespace

TEST(Lighting, KeyLightIsAboveOverTheViewersLeftShoulder)
{
    const StudioLighting lighting;
    for (double yaw = -180; yaw < 180; yaw += 15) {
        for (const double pitch : {-60.0, 0.0, 35.0, 90.0}) {
            Camera camera;
            camera.yaw = yaw * kDegree;
            camera.pitch = pitch * kDegree;
            const Vec3 key = lighting.keyDirection(camera);
            EXPECT_NEAR(key.length(), 1.0, 1e-12);
            EXPECT_NEAR(std::asin(key.z), lighting.keyElevation, 1e-9); // fixed in the world
            const Vec3 back{std::cos(camera.yaw), std::sin(camera.yaw), 0};
            EXPECT_GT(key.dot(back), 0.0) << "in front of the model, on the viewer's side";
            EXPECT_LT(key.dot(camera.right()), 0.0) << "to the viewer's left";
        }
    }
}

TEST(Lighting, FacesOfATurnedBoxDifferFromEveryAngle)
{
    const StudioLighting lighting;
    double smallest = 1e9;
    for (double turn = 0; turn < 90; turn += 5) {
        const std::vector<Face> faces = box(turn * kDegree);
        for (double yaw = -180; yaw < 180; yaw += 5) {
            for (double pitch = -80; pitch <= 90; pitch += 10) {
                Camera camera;
                camera.projection = Camera::Projection::Orthographic;
                camera.yaw = yaw * kDegree;
                camera.pitch = pitch * kDegree;
                const Vec3 toViewer = camera.backward();
                const Vec3 key = lighting.keyDirection(camera);
                std::vector<double> shade(faces.size(), -1);
                for (std::size_t i = 0; i < faces.size(); ++i)
                    if (faces[i].normal.dot(toViewer) > 0.3) // clearly visible, as the acceptance check samples
                        shade[i] = lighting.shade(faces[i].normal, toViewer, key).diffuse;
                for (std::size_t a = 0; a < faces.size(); ++a) {
                    if (shade[a] < 0)
                        continue;
                    // Readable: never black.
                    EXPECT_GE(shade[a], lighting.ground);
                    for (std::size_t b = 0; b < faces.size(); ++b) {
                        if (b == a || shade[b] < 0)
                            continue;
                        if (adjacent(faces[a], faces[b])) {
                            smallest = std::min(smallest, std::abs(shade[a] - shade[b]));
                            EXPECT_GE(std::abs(shade[a] - shade[b]), 0.08)
                                << "turn " << turn << " yaw " << yaw << " pitch " << pitch;
                        }
                        if (faces[a].kind == 1) {
                            EXPECT_GT(shade[a], shade[b] + 0.08) << "the top is the lightest";
                        }
                        if (faces[a].kind == -1) {
                            EXPECT_LT(shade[a], shade[b] - 0.08) << "the underside is the darkest";
                        }
                    }
                }
            }
        }
    }
    std::printf("smallest difference between faces meeting at an edge: %.3f\n", smallest);
}

TEST(Lighting, TwoSidedAndBounded)
{
    const StudioLighting lighting;
    Camera camera;
    const Vec3 key = lighting.keyDirection(camera);
    const Vec3 toViewer = camera.backward();
    // A normal turned away from the viewer is lit like the flipped one.
    const Vec3 n = Vec3{0.3, -0.5, 0.8}.normalized();
    EXPECT_DOUBLE_EQ(lighting.shade(n, toViewer, key).diffuse, lighting.shade(n * -1.0, toViewer, key).diffuse);
    // No surface, however turned, is lit beyond what keeps it off the
    // background's brightness (the renderer multiplies the body colour ~0.8).
    for (double a = 0; a < 360; a += 10)
        for (double b = -90; b <= 90; b += 10) {
            const Vec3 m{std::cos(b * kDegree) * std::cos(a * kDegree), std::cos(b * kDegree) * std::sin(a * kDegree),
                         std::sin(b * kDegree)};
            const auto s = lighting.shade(m, toViewer, key);
            EXPECT_GE(s.diffuse, lighting.ground);
            EXPECT_LE(s.diffuse, lighting.sky + lighting.key + lighting.fill);
            EXPECT_LE(s.diffuse, 1.12);
        }
}
