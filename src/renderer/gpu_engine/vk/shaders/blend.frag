#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "image.glsl"
#include "ramp.glsl"

// Custom blending with the samples of the current render target read as an input attachment.
// BLEND is specialized with the tvg::BlendMethod value, MATERIAL selects the source.
layout(constant_id = 0) const int BLEND = 0;
layout(constant_id = 1) const int MATERIAL = 0;  // 0 solid, 1 linear, 2 radial, 3 conic, 4 image, 5 scene

layout(input_attachment_index = 0, set = 3, binding = 0) uniform subpassInputMS uBackdrop;
layout(location = 0) in vec4 vData;  // solid color, gradient coordinate or texture coordinate

struct FragData { vec3 Sc; float Sa; float So; vec3 Dc; float Da; vec4 backdrop; };

// only the covered samples: the others may belong to another primitive of the same draw
vec4 readBackdrop()
{
    vec4 sum = vec4(0.0);
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        if ((gl_SampleMaskIn[0] & (1 << i)) == 0) continue;
        sum += subpassLoad(uBackdrop, i);
        ++count;
    }
    return sum / float(max(count, 1));
}

FragData getFragData()
{
    vec4 dst = readBackdrop();
    FragData d;
    d.Dc = dst.rgb;
    d.Da = dst.a;
    if (MATERIAL == 0) {
        d.So = 1.0;
        d.Sa = vData.a;
        d.Sc = vData.rgb * d.Sa;
    } else if (MATERIAL <= 3) {
        vec4 src;
        if (MATERIAL == 1) src = sampleRamp(linearT(vData.xy));
        else if (MATERIAL == 2) src = radialColor(vData.xy);
        else {
            src = conicColor(vData.xy);
            src.rgb /= src.a == 0.0 ? 1.0 : src.a;
        }
        d.So = 1.0;
        d.Sc = src.rgb;
        d.Sa = src.a * uPaint.options.a;
        d.Dc = dst.rgb / (dst.a == 0.0 ? 1.0 : dst.a);
        d.Da = 1.0;
        d.backdrop = dst;
    } else if (MATERIAL == 4) {
        vec4 src = sampleImage(uTexture, vData.xy);
        d.So = uPaint.options.a;
        d.Sc = src.rgb * d.So;
        d.Sa = src.a * d.So;
    } else {
        vec4 src = texture(uTexture, vData.xy);
        d.Sc = min(vec3(1.0), src.rgb / ((src.a == 0.0 || src.a == 1.0) ? 1.0 : src.a));
        d.Sa = src.a;
        d.So = pc.value;
    }
    return d;
}

vec4 postProcess(FragData d, vec4 R)
{
    if (MATERIAL >= 1 && MATERIAL <= 3) return vec4(mix(d.Sc, R.rgb, d.backdrop.a) * d.Sa + d.backdrop.rgb * (1.0 - d.Sa), d.Sa + d.backdrop.a * (1.0 - d.Sa));
    if (MATERIAL == 4) return mix(vec4(d.Dc, d.Da), R, d.Sa);
    if (MATERIAL == 5) return mix(vec4(d.Dc, d.Da), R, d.Sa * d.So);
    return R;
}

const vec3 One = vec3(1.0);
const vec3 LUM_W = vec3(0.3, 0.59, 0.11);

vec3 setLum(vec3 colorIn, float l)
{
    vec3 color = colorIn + vec3(l - dot(colorIn, LUM_W));
    float ll = l;
    float n = min(color.r, min(color.g, color.b));
    float x = max(color.r, max(color.g, color.b));
    if (n < 0.0) color = vec3(ll) + (color - vec3(ll)) * (ll / (ll - n));
    if (x > 1.0) color = vec3(ll) + (color - vec3(ll)) * ((1.0 - ll) / (x - ll));
    return color;
}

float sat(vec3 color)
{
    return max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
}

vec3 setSat(vec3 colorIn, float s)
{
    float n = min(colorIn.r, min(colorIn.g, colorIn.b));
    float x = max(colorIn.r, max(colorIn.g, colorIn.b));
    return x > n ? (colorIn - vec3(n)) * (s / (x - n)) : vec3(0.0);
}

float overlay(float s, float d, bool low)
{
    return low ? min(1.0, 2.0 * s * d) : 1.0 - min(1.0, 2.0 * (1.0 - s) * (1.0 - d));
}

float dodge(float s, float d)
{
    return d > 0.0 ? (s < 1.0 ? min(1.0, d / (1.0 - s)) : 1.0) : 0.0;
}

float burn(float s, float d)
{
    return s > 0.0 ? 1.0 - min(1.0, (1.0 - d) / s) : (d < 1.0 ? 0.0 : 1.0);
}

layout(location = 0) out vec4 outColor;

void main()
{
    FragData d = getFragData();
    vec3 Rc = d.Sc;
    vec3 Dc = d.Da > 0.0 ? min(One, d.Dc / d.Da) : d.Dc;

    if (BLEND == 1) {  // Multiply
        if (d.Da > 0.0) Rc = mix(d.Sc, d.Sc * Dc, d.Da);
    } else if (BLEND == 3) {  // Overlay
        if (d.Da > 0.0) Rc = mix(d.Sc, vec3(overlay(d.Sc.r, Dc.r, Dc.r < 0.5), overlay(d.Sc.g, Dc.g, Dc.g < 0.5), overlay(d.Sc.b, Dc.b, Dc.b < 0.5)), d.Da);
    } else if (BLEND == 4) {  // Darken
        if (d.Da > 0.0) Rc = mix(d.Sc, min(d.Sc, Dc), d.Da);
    } else if (BLEND == 5) {  // Lighten
        Rc = max(d.Sc, d.Dc);
    } else if (BLEND == 6) {  // ColorDodge
        if (d.Da > 0.0) Rc = mix(d.Sc, vec3(dodge(d.Sc.r, Dc.r), dodge(d.Sc.g, Dc.g), dodge(d.Sc.b, Dc.b)), d.Da);
    } else if (BLEND == 7) {  // ColorBurn
        if (d.Da > 0.0) Rc = mix(d.Sc, vec3(burn(d.Sc.r, Dc.r), burn(d.Sc.g, Dc.g), burn(d.Sc.b, Dc.b)), d.Da);
    } else if (BLEND == 8) {  // HardLight
        if (d.Da > 0.0) Rc = mix(d.Sc, vec3(overlay(d.Sc.r, Dc.r, d.Sc.r < 0.5), overlay(d.Sc.g, Dc.g, d.Sc.g < 0.5), overlay(d.Sc.b, Dc.b, d.Sc.b < 0.5)), d.Da);
    } else if (BLEND == 9) {  // SoftLight
        if (d.Da > 0.0) {
            vec3 Dlow = ((16.0 * Dc - 12.0) * Dc + 4.0) * Dc;
            vec3 D = mix(sqrt(Dc), Dlow, lessThanEqual(Dc, vec3(0.25)));
            vec3 low = Dc - (1.0 - 2.0 * d.Sc) * Dc * (1.0 - Dc);
            vec3 high = Dc + (2.0 * d.Sc - 1.0) * (D - Dc);
            Rc = mix(d.Sc, clamp(mix(high, low, lessThanEqual(d.Sc, vec3(0.5))), vec3(0.0), One), d.Da);
        }
    } else if (BLEND == 10) {  // Difference
        Rc = abs(d.Dc - d.Sc);
    } else if (BLEND == 12) {  // Hue
        if (d.Da > 0.0) Rc = mix(d.Sc, setLum(setSat(d.Sc, sat(Dc)), dot(Dc, LUM_W)), d.Da);
    } else if (BLEND == 13) {  // Saturation
        if (d.Da > 0.0) {
            float s = sat(d.Sc);
            float n = min(Dc.r, min(Dc.g, Dc.b));
            float x = max(Dc.r, max(Dc.g, Dc.b));
            vec3 c = x > n ? (Dc - vec3(n)) * (s / (x - n)) : vec3(0.0);
            Rc = mix(d.Sc, setLum(c, dot(Dc, LUM_W)), d.Da);
        }
    } else if (BLEND == 14) {  // Color
        if (d.Da > 0.0) Rc = mix(d.Sc, setLum(d.Sc, dot(Dc, LUM_W)), d.Da);
    } else if (BLEND == 15) {  // Luminosity
        if (d.Da > 0.0) Rc = mix(d.Sc, setLum(Dc, dot(d.Sc, LUM_W)), d.Da);
    } else {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);  // debug: no custom shader for this method
        return;
    }
    outColor = postProcess(d, vec4(Rc, 1.0));
}
