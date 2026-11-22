// Gradient color ramps live in one atlas: 512 texels per row, one row per gradient.
layout(set = 1, binding = 0) uniform sampler2D uTexture;

const float GRADIENT_SIZE = 512.0;
const float INV_TAU = 0.15915494309189535;

int rampRow()
{
    return int(uPaint.options.y);
}

vec4 sampleRamp(float t)
{
    float v = (uPaint.options.y + 0.5) / float(textureSize(uTexture, 0).y);
    return texture(uTexture, vec2(t, v));
}

float linearT(vec2 pos)
{
    vec2 st = uPaint.coords.xy;
    vec2 ba = uPaint.coords.zw - st;
    return dot(pos - st, ba) / dot(ba, ba);
}

float radialT(vec2 pos)
{
    vec2 d0 = pos - uPaint.coords.xy;
    vec2 d1 = uPaint.coords.xy - uPaint.focal.xy;
    float r0 = uPaint.coords.z;
    float rd = uPaint.focal.z - uPaint.coords.z;
    float a = dot(d1, d1) - rd * rd;
    float b = 2.0 * dot(d0, d1) - 2.0 * r0 * rd;
    float c = dot(d0, d0) - r0 * r0;
    float d = b * b - 4.0 * a * c;
    float t = 0.0;
    if (d >= 0.0) t = min(1.0, (-b + sqrt(d)) / (2.0 * a));
    if (c > 0.0 && t >= 1.0) t = 0.0;
    return t;
}

vec4 radialColor(vec2 pos)
{
    if (uPaint.coords.z == 0.0) return texelFetch(uTexture, ivec2(int(GRADIENT_SIZE) - 1, rampRow()), 0);
    return sampleRamp(1.0 - radialT(pos));
}

// Returns a premultiplied color.
vec4 conicColor(vec2 pos)
{
    float width = fwidth(pos.y);
    if (pos.x >= 0.0 && abs(pos.y) < 0.5 * width) {
        vec4 first = texelFetch(uTexture, ivec2(0, rampRow()), 0);
        vec4 last = texelFetch(uTexture, ivec2(int(GRADIENT_SIZE) - 1, rampRow()), 0);
        float coverage = pos.y / width + 0.5;
        // Mix coverage after premultiplication so transparent stops cannot tint the seam.
        return mix(vec4(last.rgb * last.a, last.a), vec4(first.rgb * first.a, first.a), coverage);
    }
    float t = fract(atan(pos.y, pos.x) * INV_TAU);
    float u = (t * (GRADIENT_SIZE - 1.0) + 0.5) / GRADIENT_SIZE;
    float v = (uPaint.options.y + 0.5) / float(textureSize(uTexture, 0).y);
    // Explicit LOD avoids an anisotropic footprint across the wrapped seam coordinate.
    vec4 color = textureLod(uTexture, vec2(u, v), 0.0);
    return vec4(color.rgb * color.a, color.a);
}
