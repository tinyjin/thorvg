// Shared declarations. Every graphics pipeline uses one pipeline layout:
//   set 0: dynamic uniform buffers (view matrix, paint settings) in the frame buffer
//   set 1: primary texture (gradient ramp atlas, image, scene source)
//   set 2: secondary texture (mask, shadow)
//   set 3: input attachment of the multisampled color (custom blend backdrop)
//   push constants: effect parameters and a scalar (opacity or clip depth)

layout(set = 0, binding = 0) uniform ViewBlock { mat4 uViewMat; };
layout(set = 0, binding = 1) uniform PaintBlock { vec4 options; mat4 transform; vec4 coords; vec4 focal; } uPaint;
layout(push_constant) uniform PushBlock { vec4 params[3]; float value; } pc;

// options: [0] color space, [1] gradient ramp row, [3] opacity

vec4 placeShape(vec2 position, vec4 adjust)
{
    vec4 pos = uViewMat * vec4(position, 0.0, 1.0);
    // The scene adjustment maps canvas clip space into the current render target.
    vec4 result = vec4(pos.xy * adjust.xy + adjust.zw * pos.w, pos.zw);
    // WebGPU clip space points y up, Vulkan points y down.
    result.y = -result.y;
    return result;
}

vec4 placeQuad(vec2 position)
{
    return vec4(position.x, -position.y, 0.0, 1.0);
}

