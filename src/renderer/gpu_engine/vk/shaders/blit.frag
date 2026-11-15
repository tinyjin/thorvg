#version 450

layout(constant_id = 0) const bool UNPREMULTIPLY = false;

layout(set = 1, binding = 0) uniform sampler2D uSource;
layout(location = 0) in vec4 vTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 src = texture(uSource, vTexCoord.xy);
    if (UNPREMULTIPLY) outColor = src.a <= 0.0 ? vec4(0.0) : vec4(clamp(src.rgb / src.a, vec3(0.0), vec3(1.0)), src.a);
    else outColor = src;
}
