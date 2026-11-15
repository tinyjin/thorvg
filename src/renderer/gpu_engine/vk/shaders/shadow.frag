#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D uSource;
layout(set = 2, binding = 0) uniform sampler2D uShadow;
layout(location = 0) in vec4 vTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(uSource, 0));
    vec2 offset = pc.params[2].xy * texelSize;
    vec4 orig = texture(uSource, vTexCoord.xy);
    vec4 blur = texture(uShadow, vTexCoord.xy - offset);
    outColor = orig + pc.params[1] * blur.a * (1.0 - orig.a);
}
