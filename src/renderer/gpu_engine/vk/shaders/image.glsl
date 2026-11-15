// Images keep the full resolution down to half scale like the software rasterizer, and below it sample
// one mip level finer than the footprint, close to its sparse area averaging.
vec4 sampleImage(sampler2D image, vec2 uv)
{
    vec2 texels = uv * vec2(textureSize(image, 0));
    float lod = 0.5 * log2(max(dot(dFdx(texels), dFdx(texels)), dot(dFdy(texels), dFdy(texels))));
    return textureLod(image, uv, max(lod - 1.0, 0.0));
}
