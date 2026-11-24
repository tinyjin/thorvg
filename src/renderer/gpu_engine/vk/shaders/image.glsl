// Images keep the full resolution down to half scale like the software rasterizer, and below it sample
// one mip level finer than the footprint, close to its sparse area averaging.
vec4 sampleImage(sampler2D image, vec2 uv)
{
    return texture(image, uv, -1.0);
}
