#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

layout(location = 0) in vec3 vNormal; // world
layout(location = 1) in vec3 vWorld;
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
    vec3 n = normalize(vNormal);
    vec3 toViewer = eye.w > 0.5 ? normalize(eye.xyz - vWorld) : eye.xyz;
    // Two-sided without relying on winding (backends differ in Y-flip): the
    // side we see faces us.
    if (dot(n, toViewer) < 0.0)
        n = -n;

    if (params2.x > 0.5) {
        // Tint / overlay: mostly flat, with a hint of shape.
        float shade = 0.82 + 0.18 * dot(n, toViewer);
        fragColor = vec4(color.rgb * shade, color.a);
        return;
    }

    // Studio lighting in world space (core/Lighting.h, StudioLighting::shade):
    // sky/ground ambient (up-facing faces lightest, down-facing darkest), a
    // key light from above, a gentle fill from the viewer, a soft highlight.
    float ambient = mix(lights.y, lights.x, n.z * 0.5 + 0.5);
    float key = max(dot(n, light.xyz), 0.0);
    float fill = max(dot(n, toViewer), 0.0);
    vec3 halfway = normalize(light.xyz + toViewer);
    float highlight = pow(max(dot(n, halfway), 0.0), gloss.y);
    vec3 lit = color.rgb * (ambient + lights.z * key + lights.w * fill) + vec3(gloss.x * highlight);
    fragColor = vec4(lit, color.a);
}
