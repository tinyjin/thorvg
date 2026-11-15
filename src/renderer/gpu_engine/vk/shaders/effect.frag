#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// EFFECT: 0 vertical gaussian, 1 horizontal gaussian, 2 motion blur, 3 fill, 4 tint, 5 tritone
// params, see VkShaderTypeEffectParams
layout(constant_id = 0) const int EFFECT = 0;

layout(set = 1, binding = 0) uniform sampler2D uSource;
layout(location = 0) in vec4 vTexCoord;
layout(location = 0) out vec4 outColor;

float gaussian(float x, float sigma)
{
    return exp(-x * x / (2.0 * sigma * sigma)) * 0.39894 / sigma;
}

vec4 blur(vec2 axis)
{
    vec2 texelSize = 1.0 / vec2(textureSize(uSource, 0));
    float sigma = pc.params[0].x * pc.params[0].y;
    int radius = int(pc.params[0].z);
    vec4 colorSum = vec4(0.0);
    float weightSum = 0.0;
    for (int i = -radius; i <= radius; i++) {
        vec2 coord = vTexCoord.xy + axis * (float(i) * texelSize);
        float c = dot(coord, axis);
        float weight = clamp(c, 0.0, 1.0) == c ? gaussian(float(i), sigma) : 0.0;
        colorSum += texture(uSource, coord) * weight;
        weightSum += weight;
    }
    return colorSum / weightSum;
}

void main()
{
    if (EFFECT == 0) {
        outColor = blur(vec2(0.0, 1.0));
    } else if (EFFECT == 1) {
        outColor = blur(vec2(1.0, 0.0));
    } else if (EFFECT == 2) {
        // Clamp sampling coordinates to the region's outermost texel centers.
        vec2 invTextureSize = 1.0 / vec2(textureSize(uSource, 0));
        vec2 minCoord = pc.params[1].xy + vec2(0.5);
        vec2 maxCoord = pc.params[1].xy + pc.params[1].zw - vec2(0.5);
        int samples = int(pc.params[0].z);
        float invSamples = 1.0 / float(samples);
        vec4 color = vec4(0.0);
        for (int i = 0; i < samples; i++) {
            float t = (float(i) + 0.5) * invSamples - 0.5;
            vec2 p = clamp(vTexCoord.xy / invTextureSize + pc.params[0].xy * t, minCoord, maxCoord);
            color += textureLod(uSource, p * invTextureSize, 0.0);
        }
        outColor = color * invSamples;
    } else if (EFFECT == 3) {
        vec4 orig = texture(uSource, vTexCoord.xy);
        outColor = pc.params[0] * orig.a * pc.params[0].a;
    } else if (EFFECT == 4) {
        vec4 orig = texture(uSource, vTexCoord.xy);
        float luma = dot(orig.rgb, vec3(0.2126, 0.7152, 0.0722));
        outColor = vec4(mix(orig.rgb, mix(pc.params[0].rgb, pc.params[1].rgb, luma), pc.params[2].r) * orig.a, orig.a);
    } else {
        vec4 orig = texture(uSource, vTexCoord.xy);
        float luma = dot(orig.rgb, vec3(0.2126, 0.7152, 0.0722));
        bool bright = luma >= 0.5;
        float t = bright ? (luma - 0.5) * 2.0 : luma * 2.0;
        vec3 from = bright ? pc.params[1].rgb : pc.params[0].rgb;
        vec3 to = bright ? pc.params[2].rgb : pc.params[1].rgb;
        vec4 tmp = vec4(mix(from, to, t), 1.0);
        if (pc.params[2].a > 0.0) tmp = mix(tmp, orig, pc.params[2].a);
        outColor = tmp * orig.a;
    }
}
