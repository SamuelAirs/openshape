#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 vNormal; // world
layout(location = 1) out vec3 vWorld;

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

// Moves a point towards the viewer by `params.w` device pixels' worth of
// depth at its distance, keeping its place on screen (sketch fills lying on
// faces). Scale-free, unlike a fixed depth-buffer offset, which in
// perspective came to centimetres at the model and showed hidden edges.
vec4 biasedClip(vec3 p)
{
    vec4 v = view * vec4(p, 1.0);
    if (eye.w > 0.5)
        v.xyz *= max(1.0 - params.w * camera.x, 0.0); // along the ray to the eye
    else
        v.z += params.w * camera.x;
    return proj * v;
}

void main()
{
    vNormal = normal; // meshes are in world coordinates
    vWorld = position;
    gl_Position = params.w != 0.0 ? biasedClip(position) : mvp * vec4(position, 1.0);
}
