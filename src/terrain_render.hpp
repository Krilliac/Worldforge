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
void rasterTerrainSplat(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                        const std::vector<TerrainLayer>& layers, float tiling, Vec3 lightDir);
// Coloured-light overload: ambient/diffuse from zone lighting (Light.dbc).
void rasterTerrainSplat(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                        const std::vector<TerrainLayer>& layers, float tiling,
                        const ShadeLight& light);

} // namespace wf
