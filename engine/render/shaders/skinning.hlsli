// Linear blend skinning (Milestone 8C, part 1, ADR-036), shared by mesh.hlsl and shadow.hlsl. The same sum as the CPU
// reference (asset/skinning.h): a vertex is moved by up to four joints' matrices, weighted.
//
// Both buffers are read through their device addresses (push constants `skin`):
//   skin.xy: this mesh's skin data, 24 bytes a vertex: four 16-bit joint indices (two uints), four float weights;
//   skin.zw: this draw's palette in the frame's palette buffer, 48 bytes a joint: the rows of world x inverse bind.
// An address of 0 means not skinned (the instance's world rows are used, as for every other mesh).

uint64_t skin_address(uint2 v) { return (uint64_t(v.y) << 32) | uint64_t(v.x); }

bool skinned(uint4 skin) { return (skin.x | skin.y) != 0u; }

// The weighted sum of the vertex's joints' matrices: rows of world from object, like an instance's.
float3x4 skin_matrix(uint4 skin, uint vertex)
{
    const uint64_t s = skin_address(skin.xy) + uint64_t(vertex) * 24u;
    const uint2 packed = vk::RawBufferLoad<uint2>(s);
    const float4 w = vk::RawBufferLoad<float4>(s + 8u);
    const uint joints[4] = {packed.x & 0xffffu, packed.x >> 16, packed.y & 0xffffu, packed.y >> 16};
    const uint64_t palette = skin_address(skin.zw);
    float3x4 m = (float3x4)0;
    [unroll] for (uint k = 0; k < 4; ++k)
    {
        if (w[k] == 0.0) continue;
        const uint64_t a = palette + uint64_t(joints[k]) * 48u;
        m += w[k] * float3x4(vk::RawBufferLoad<float4>(a), vk::RawBufferLoad<float4>(a + 16u), vk::RawBufferLoad<float4>(a + 32u));
    }
    return m;
}
