#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 vNormal;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    mat4 view;
    vec4 color;
    vec4 params;  // x,y: viewport px, z: line width px, w: depth bias
    vec4 params2; // x: shading mode (0 lit, 1 tint, 2 overlay)
};

void main()
{
    vNormal = mat3(view) * normal;
    gl_Position = mvp * vec4(position, 1.0);
    // Optional depth bias (sketch profile fills lying on body faces).
    gl_Position.z -= params.w * gl_Position.w;
}
