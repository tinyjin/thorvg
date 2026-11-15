#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 2) in vec4 inAdjust;
layout(location = 0) out vec4 vGradCoord;

void main()
{
    gl_Position = placeShape(inPosition, inAdjust);
    vGradCoord = uPaint.transform * vec4(inPosition, 0.0, 1.0);
}
