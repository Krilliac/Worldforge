#pragma once
// ---------------------------------------------------------------------------
// terrain_render: render terrain with the MCNK/MCAL multi-layer alpha splat --
// the textured terrain path. Layer 0 is the opaque base; layers 1..3 are blended
// on top by their 64x64 alpha coverage maps (decodeAlphaMap). Builds a TexMesh
// with chunk-space UVs from a MapChunk, then rasterises it sampling all layers
// per fragment. The biggest visual jump from height-shaded to real WoW terrain.
// ---------------------------------------------------------------------------
#include <vector>

#include "image.hpp"
#include "math.hpp"
#include "terrain.hpp"    // MapChunk, AlphaMap
#include "raster.hpp"     // TexMesh, Framebuffer

namespace wf {

// The client's exact MCSH darkening factor: a shadowed terrain texel is scaled
// by 178/256 (the engine computes 178 * value >> 8, ~0.695). Kept exact -- not
// an eyeballed constant -- so rasteriser readbacks can assert the ratio.
constexpr float kMcshShadowFactor = 178.0f / 256.0f;

struct TerrainLayer {
    const Image*    texture = nullptr;   // the layer's BLP-decoded tile
    const AlphaMap* alpha   = nullptr;   // 64x64 coverage; null = full (base layer)
};

// Build a chunk's mesh with UVs = chunk-space (0..1) coords, so the splat can
// sample the alpha maps and tile the textures. Same geometry as buildChunkMesh.
TexMesh buildChunkTexMesh(const MapChunk& mc, int blockX, int blockY);

// Blend the layers at chunk-uv (u,v in 0..1); `tiling` = texture repeats/chunk.
Rgba splatSample(const std::vector<TerrainLayer>& layers, float u, float v, float tiling);

// Rasterise a terrain TexMesh (uv = chunk 0..1) with the layer splat + lighting.
// `shadow`, when non-null, is the chunk's raw 64x64-bit MCSH map: texels whose
// bit is set are darkened by exactly kMcshShadowFactor (baked terrain
// self-shadow). Pass null for no shadow.
void rasterTerrainSplat(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                        const std::vector<TerrainLayer>& layers, float tiling, Vec3 lightDir,
                        const std::vector<uint8_t>* shadow = nullptr);
// Coloured-light overload: ambient/diffuse from zone lighting (Light.dbc).
void rasterTerrainSplat(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                        const std::vector<TerrainLayer>& layers, float tiling,
                        const ShadeLight& light,
                        const std::vector<uint8_t>* shadow = nullptr);

} // namespace wf
