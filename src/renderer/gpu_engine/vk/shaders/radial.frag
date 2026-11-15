#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "ramp.glsl"

layout(location = 0) in vec4 vGradCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 Sc = sampleRamp(1.0 - radialT(vGradCoord.xy));
    float So = uPaint.options.a;
    outColor = vec4(Sc.rgb * Sc.a * So, Sc.a * So);
}
