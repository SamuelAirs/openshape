#version 440

// Screen-space expanded line segments: every segment is a quad of 4 vertices
// that all carry both endpoints; `corner` picks the end and the side.
layout(location = 0) in vec3 p0;
layout(location = 1) in vec3 p1;
layout(location = 2) in vec2 corner; // x: 0 = start, 1 = end; y: -1/+1 side

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    mat4 view;
    vec4 color;
    vec4 params;  // x,y: viewport px, z: line width px, w: depth bias
    vec4 params2;
};

void main()
{
    vec4 c0 = mvp * vec4(p0, 1.0);
    vec4 c1 = mvp * vec4(p1, 1.0);
    vec2 halfViewport = params.xy * 0.5;
    vec2 s0 = c0.xy / c0.w * halfViewport;
    vec2 s1 = c1.xy / c1.w * halfViewport;
    vec2 d = s1 - s0;
    float len = length(d);
    vec2 dir = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 side = vec2(-dir.y, dir.x);

    bool atEnd = corner.x > 0.5;
    vec4 c = atEnd ? c1 : c0;
    float halfWidth = params.z * 0.5;
    // Extend slightly past the endpoints so joints between segments close.
    vec2 offset = side * corner.y * halfWidth + dir * (atEnd ? halfWidth : -halfWidth) * 0.5;
    c.xy += offset / halfViewport * c.w;
    c.z -= params.w * c.w;
    gl_Position = c;
}
