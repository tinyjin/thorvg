#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 2) in vec4 inAdjust;

void main()
{
    gl_Position = placeShape(inPosition, inAdjust);
}
