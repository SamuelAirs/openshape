#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The ground grid, drawn per pixel on one quad: minor and major lines
// (anti-aliased, `params.z` device pixels wide) that fade out smoothly
// towards the grid's radius, with distance from the eye in perspective,
// where their neighbours come closer than a few pixels (a receding plane)
// and when the plane is seen nearly edge-on, so there is no hard border and
// no moire. The X and Y axes are drawn separately (lines, faded later).
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

// Coverage of the lines at whole values of `c` (grid cells along one axis)
// except the one at `axis`. `g` is how many cells one device pixel spans
// across the lines.
float lines(float c, float g, float axis, float width)
{
    float cells = abs(fract(c + 0.5) - 0.5); // distance to the nearest line
    float coverage = clamp(0.5 * width + 0.5 - cells / max(g, 1e-7), 0.0, 1.0);
    if (abs(floor(c + 0.5) - axis) < 0.5)
        coverage = 0.0;
    // Lines closer together than ~10 px start to fade, gone at 4 px.
    return coverage * (1.0 - smoothstep(0.1, 0.25, g));
}

float gridAlpha(vec2 rel, float step, float width)
{
    vec2 c = rel / step;
    // Derivatives first, in uniform control flow.
    float gx = length(vec2(dFdx(c.x), dFdy(c.x)));
    float gy = length(vec2(dFdx(c.y), dFdy(c.y)));
    vec2 axis = -grid.xy / step; // whole numbers: the center lies on a major line
    return max(lines(c.x, gx, axis.x, width), lines(c.y, gy, axis.y, width));
}

void main()
{
    vec2 rel = vWorld.xy - grid.xy;
    float minor = gridAlpha(rel, grid.z, params.z);
    float major = gridAlpha(rel, grid.z * 10.0, params.z);
    float alpha = max(minor * params2.y, major * params2.z);

    alpha *= 1.0 - smoothstep(0.5 * fade.x, fade.x, length(rel));
    vec3 toEye = eye.w > 0.5 ? eye.xyz - vWorld : eye.xyz;
    if (fade.w > 0.0)
        alpha *= 1.0 - smoothstep(fade.z, fade.w, length(toEye));
    // Nearly edge-on, even the major lines crowd together.
    alpha *= smoothstep(0.03, 0.2, abs(normalize(toEye).z));
    fragColor = vec4(color.rgb, alpha);
}
