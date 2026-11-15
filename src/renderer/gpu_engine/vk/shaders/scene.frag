#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec4 vTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 Sc = texture(uTexture, vTexCoord.xy);
    outColor = Sc * pc.value;
}
