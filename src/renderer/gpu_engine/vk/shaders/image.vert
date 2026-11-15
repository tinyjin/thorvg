#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inAdjust;
layout(location = 0) out vec4 vTexCoord;

void main()
{
    gl_Position = placeShape(inPosition, inAdjust);
    vTexCoord = vec4(inTexCoord, 0.0, 0.0);
}
