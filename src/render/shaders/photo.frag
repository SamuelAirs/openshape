#version 440
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A reference photo's picture at its opacity (`color.w`). Seen from behind
// its plane it shows mirrored (the quad has no back face culled), dimmer
// and greyer, so the front reads as the front.
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

// Shared by every shader (ViewportRenderer.cpp, UniformData).
layout(std140, binding = 0) uniform Frame {
    mat4 mvp;     // clip from world
    mat4 view;    // view from world (the camera looks down -z)
    mat4 proj;    // clip from view
    vec4 color;   // photos: white, w = opacity
    vec4 params;  // x,y: viewport (device px), z: line width (device px), w: depth bias (device px towards the viewer)
    vec4 params2; // x: mode (meshes: 0 lit, 1 tint, 2 overlay; lines: 1 fading like the axes); grid: y minor, z major alpha
    vec4 eye;     // xyz: eye (perspective) or unit direction towards the viewer (orthographic); w: 1 perspective
    vec4 camera;  // x: world size of a device pixel (orthographic; at view depth 1 in perspective)
    vec4 light;   // xyz: unit direction towards the key light (world)
    vec4 lights;  // x: sky ambient, y: ground ambient, z: key, w: fill from the viewer (core/Lighting.h)
    vec4 gloss;   // x: specular strength, y: specular exponent
    vec4 grid;    // xy: grid center (z = 0), z: minor step; photos: xyz the plane's front normal
    vec4 fade;    // x: grid radius (shadows: blur), y: axis radius, z/w: eye distances where the grid starts/ends fading (0: never)
};

layout(binding = 1) uniform sampler2D picture;

void main()
{
    vec4 texel = texture(picture, vUv);
    vec3 towardsEye = eye.w > 0.5 ? eye.xyz - vWorld : eye.xyz;
    float alpha = texel.a * color.a;
    vec3 rgb = texel.rgb;
    if (dot(grid.xyz, towardsEye) < 0.0) {
        float grey = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb = mix(rgb, vec3(grey), 0.5);
        alpha *= 0.5;
    }
    fragColor = vec4(rgb, alpha);
}
