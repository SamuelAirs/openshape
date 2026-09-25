#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

layout(location = 0) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    mat4 view;
    vec4 color;
    vec4 params;
    vec4 params2;
};

void main()
{
    vec3 n = normalize(vNormal);
    // Two-sided lighting without relying on winding (backend Y-flips differ):
    // in view space the viewer looks down -Z, so visible faces have n.z > 0.
    if (n.z < 0.0)
        n = -n;

    if (params2.x > 0.5) {
        // Tint / overlay: mostly flat, with a hint of shape.
        float shade = 0.82 + 0.18 * n.z;
        fragColor = vec4(color.rgb * shade, color.a);
        return;
    }

    const vec3 keyDir = normalize(vec3(-0.35, 0.55, 0.76));
    const vec3 fillDir = normalize(vec3(0.6, -0.25, 0.45));
    float key = max(dot(n, keyDir), 0.0);
    float fill = max(dot(n, fillDir), 0.0);
    float rim = pow(1.0 - n.z, 3.0);
    vec3 halfVec = normalize(keyDir + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, halfVec), 0.0), 48.0);

    // Bright, soft studio lighting: high ambient keeps every face readable,
    // the key light separates face orientations.
    vec3 lit = color.rgb * (0.60 + 0.32 * key + 0.14 * fill) + vec3(0.08) * spec + vec3(0.04) * rim;
    fragColor = vec4(lit, color.a);
}
