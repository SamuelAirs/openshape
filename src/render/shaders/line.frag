#version 440

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
    fragColor = color;
}
