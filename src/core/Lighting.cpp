// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Lighting.h"

#include <algorithm>
#include <cmath>

namespace os {

Vec3 StudioLighting::keyDirection(const Camera& camera) const
{
    // Horizontal directions from the camera's yaw alone (as Camera::right()).
    const Vec3 back{std::cos(camera.yaw), std::sin(camera.yaw), 0};
    const Vec3 right{-std::sin(camera.yaw), std::cos(camera.yaw), 0};
    const Vec3 horizontal = back * std::cos(keyAzimuth) - right * std::sin(keyAzimuth);
    return (horizontal * std::cos(keyElevation) + Vec3{0, 0, 1} * std::sin(keyElevation)).normalized();
}

StudioLighting::Shade StudioLighting::shade(Vec3 normal, const Vec3& toViewer, const Vec3& keyDir) const
{
    if (normal.dot(toViewer) < 0)
        normal = normal * -1.0;
    const double up = normal.z * 0.5 + 0.5;
    const double ambient = ground + (sky - ground) * up;
    const double keyLight = std::max(normal.dot(keyDir), 0.0);
    const double fillLight = std::max(normal.dot(toViewer), 0.0);
    const Vec3 halfway = (keyDir + toViewer).normalized();
    const double highlight = std::pow(std::max(normal.dot(halfway), 0.0), shininess);
    return {ambient + key * keyLight + fill * fillLight, specular * highlight};
}

} // namespace os
