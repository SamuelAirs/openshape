#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen-space expanded line segments: every segment is a quad of 4 vertices
// that all carry both endpoints; `corner` picks the end and the side.
layout(location = 0) in vec3 p0;
layout(location = 1) in vec3 p1;
layout(location = 2) in vec2 corner; // x: 0 = start, 1 = end; y: -1/+1 side

layout(location = 0) out vec3 vWorld; // the point on the segment (for fading)

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
// depth at its distance, keeping its place on screen, so edges win the depth
// test against the faces they bound. Scale-free, unlike a fixed
// depth-buffer offset, which in perspective came to centimetres at the model
// and showed hidden edges through the body.
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
    vec4 c0 = biasedClip(p0);
    vec4 c1 = biasedClip(p1);
    vec2 halfViewport = params.xy * 0.5;
    vec2 s0 = c0.xy / c0.w * halfViewport;
    vec2 s1 = c1.xy / c1.w * halfViewport;
    vec2 d = s1 - s0;
    float len = length(d);
    vec2 dir = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 side = vec2(-dir.y, dir.x);

    bool atEnd = corner.x > 0.5;
    vec4 c = atEnd ? c1 : c0;
    vWorld = atEnd ? p1 : p0;
    float halfWidth = params.z * 0.5;
    // Extend slightly past the endpoints so joints between segments close.
    // Zero-length segments (point markers) extend fully and become squares.
    float cap = len > 1e-6 ? 0.5 : 1.0;
    vec2 offset = side * corner.y * halfWidth + dir * (atEnd ? halfWidth : -halfWidth) * cap;
    c.xy += offset / halfViewport * c.w;
    gl_Position = c;
}
