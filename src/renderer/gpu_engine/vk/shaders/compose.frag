#version 450

// MASK is specialized with the tvg::MaskMethod value.
layout(constant_id = 0) const int MASK = 0;

layout(set = 1, binding = 0) uniform sampler2D uSource;
layout(set = 2, binding = 0) uniform sampler2D uMask;
layout(location = 0) in vec4 vTexCoord;
layout(location = 1) in vec2 vMaskCoord;
layout(location = 0) out vec4 outColor;

vec4 readMask(vec2 coord)
{
    vec4 color = texture(uMask, coord);
    return all(greaterThanEqual(coord, vec2(0.0))) && all(lessThan(coord, vec2(1.0))) ? color : vec4(0.0);
}

void main()
{
    vec4 src = texture(uSource, vTexCoord.xy);
    if (MASK == 0) {
        outColor = src;
        return;
    }
    vec4 msk = readMask(vMaskCoord);
    float luma = dot(msk.rgb, vec3(0.2125, 0.7154, 0.0721));
    if (MASK == 1) outColor = src * msk.a;                                                  // Alpha
    else if (MASK == 2) outColor = src * (1.0 - msk.a);                                     // InvAlpha
    else if (MASK == 3) outColor = src * luma;                                              // Luma
    else if (MASK == 4) outColor = src * (1.0 - luma);                                      // InvLuma
    else if (MASK == 5) outColor = vec4(src.rgb, src.a + msk.a * (1.0 - src.a));            // Add
    else if (MASK == 6) outColor = vec4(src.rgb, src.a * (1.0 - msk.a));                    // Subtract
    else if (MASK == 7) outColor = vec4(src.rgb, src.a * msk.a);                            // Intersect
    else if (MASK == 8) outColor = vec4(src.rgb, src.a * (1.0 - msk.a) + msk.a * (1.0 - src.a));  // Difference
    else if (MASK == 9) outColor = vec4(src.rgb, max(src.a, msk.a));                        // Lighten
    else outColor = vec4(src.rgb, min(src.a, msk.a));                                       // Darken
}
