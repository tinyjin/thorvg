#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inAdjust;
layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = placeShape(inPosition, inAdjust);
    vColor = inColor;
}
