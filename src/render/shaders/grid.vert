#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A flat quad on the ground plane (z = 0): the grid and contact shadows are
// drawn per pixel on it (grid.frag, shadow.frag).
layout(location = 0) in vec3 position;
layout(location = 0) out vec3 vWorld;

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

// Moves a point along its view ray by `params.w` device pixels' worth of
// depth (negative: away from the viewer, so a body's bottom face lying on
// the ground hides the grid instead of fighting it).
vec4 biasedClip(vec3 p)
{
    vec4 v = view * vec4(p, 1.0);
    if (eye.w > 0.5)
        v.xyz *= max(1.0 - params.w * camera.x, 0.0);
    else
        v.z += params.w * camera.x;
    return proj * v;
}

void main()
{
    vWorld = position;
    gl_Position = biasedClip(position);
}
