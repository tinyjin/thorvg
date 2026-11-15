#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 3) in vec2 inMaskCoord;
layout(location = 0) out vec4 vTexCoord;
layout(location = 1) out vec2 vMaskCoord;

void main()
{
    gl_Position = placeQuad(inPosition);
    vTexCoord = vec4(inTexCoord, 0.0, 0.0);
    vMaskCoord = inMaskCoord;
}
