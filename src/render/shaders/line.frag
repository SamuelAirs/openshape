#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

layout(location = 0) in vec3 vWorld;
layout(location = 0) out vec4 fragColor;

// Shared by every shader (ViewportRenderer.cpp, UniformData).
layout(std140, binding = 0) uniform Frame {
    mat4 mvp;     // clip from world
    mat4 view;    // view from world (the camera looks down -z)
    mat4 proj;    // clip from view
    vec4 color;
    vec4 params;  // x,y: viewport (device px), z: line width (device px), w: depth bias (device px towards the viewer)
    vec4 params2; // x: mode (meshes: 0 lit, 1 tint, 2 overlay; lines: 1 fading like the axes); grid: y minor, z major alpha
    vec4 eye;     // xyz: eye (perspective) or unit direction towards the viewer (orthographic); w: 1 perspective
    vec4 camera;  // x: world size of a device pixel (orthographic; at view depth 1 in perspective)
    vec4 light;   // xyz: unit direction towards the key light (world)
    vec4 lights;  // x: sky ambient, y: ground ambient, z: key, w: fill from the viewer (core/Lighting.h)
    vec4 gloss;   // x: specular strength, y: specular exponent
    vec4 grid;    // xy: grid center (z = 0), z: minor step
    vec4 fade;    // x: grid radius (shadows: blur), y: axis radius, z/w: eye distances where the grid starts/ends fading (0: never)
};

void main()
{
    float alpha = color.a;
    if (params2.x > 0.5) {
        // The X/Y/Z axes fade out like the grid, further out (grid.frag).
        float reach = fade.y / max(fade.x, 1e-6);
        alpha *= 1.0 - smoothstep(0.5 * fade.y, fade.y, length(vWorld - vec3(grid.xy, 0.0)));
        if (fade.w > 0.0) {
            float toEye = length(eye.xyz - vWorld);
            alpha *= 1.0 - smoothstep(fade.z * reach, fade.w * reach, toEye);
            // Close to the eye (the Z axis seen from above) a line would
            // sweep across the whole view: it fades out there too.
            alpha *= smoothstep(0.25 * fade.z, 0.5 * fade.z, toEye);
        }
        // The Z axis's half on the far side of the ground (below it, seen
        // from above) is faint: drawn fully it read as a line on the ground
        // running towards the viewer. eye.z is the eye's height, or in
        // orthographic the view direction's (the same sign).
        if (vWorld.z * eye.z < 0.0)
            alpha *= 0.3;
    }
    fragColor = vec4(color.rgb, alpha);
}
