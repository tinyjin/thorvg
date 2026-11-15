#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "ramp.glsl"

layout(location = 0) in vec4 vGradCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = conicColor(vGradCoord.xy) * uPaint.options.a;
}
