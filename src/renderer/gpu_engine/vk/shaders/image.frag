#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "image.glsl"

layout(set = 1, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec4 vTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 Sc = sampleImage(uTexture, vTexCoord.xy);
    float So = uPaint.options.a;
    outColor = vec4(Sc.rgb * So, Sc.a * So);
}
