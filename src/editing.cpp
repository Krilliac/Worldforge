#include "editing.hpp"

#include <algorithm>
#include <cmath>

#include "coords.hpp"

namespace wf {

namespace {
// Visit every MCVT height sample of a chunk, yielding (mcvtIndex, worldX, worldY)
// with the exact placement buildChunkMesh (terrain.cpp) uses: the outer 9x9 ring
// (sample i*17+j) then the inner 8x8 grid (sample i*17+9+j), the latter offset
// half a cell into each quad. Keeps the brush math identical to the render mesh.
template <class F>
void forEachChunkSample(const MapChunk& mc, int blockX, int blockY, F&& fn) {
    const int   col    = static_cast<int>(mc.indexX);   // west-east
    const int   row    = static_cast<int>(mc.indexY);   // north-south
    const Vec3  corner = chunkCornerWorld(blockX, blockY, row, col, mc.position.z);
    const float U      = static_cast<float>(UNIT_SIZE);
    for (int i = 0; i < 9; ++i)
        for (int j = 0; j < 9; ++j)
            fn(i * 17 + j, corner.x - i * U, corner.y - j * U);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            fn(i * 17 + 9 + j, corner.x - (i + 0.5f) * U, corner.y - (j + 0.5f) * U);
}
} // namespace

float falloffWeight(Falloff f, float dist, float radius, float innerRatio) {
    if (radius <= 0.0f) return 0.0f;
    if (dist >= radius) return 0.0f;
    if (dist <= 0.0f)   return 1.0f;

    const float inner = std::clamp(innerRatio, 0.0f, 1.0f) * radius;
    if (dist <= inner) return 1.0f;

    // Remap the falloff band [inner, radius] -> t in [0,1] (0 at inner edge).
    const float span = radius - inner;
    const float t = (span > 0.0f) ? (dist - inner) / span : 1.0f;  // 0..1

    switch (f) {
        case Falloff::Flat:   return 1.0f;
        case Falloff::Linear: return 1.0f - t;
        case Falloff::Smooth: {                       // smoothstep, 1 -> 0
            const float s = t * t * (3.0f - 2.0f * t);
            return 1.0f - s;
        }
        case Falloff::Gaussian: {                     // bell, ~0 at the radius
            const float k = 2.5f;                     // shape: e^-(k*t)^2
            return std::exp(-(k * t) * (k * t));
        }
    }
    return 0.0f;
}

int brushRaiseLower(std::vector<Vertex>& verts, const Brush& b, float sign) {
    int hits = 0;
    for (Vertex& v : verts) {
        const float dx = v.position.x - b.center.x;
        const float dy = v.position.y - b.center.y;
        const float d  = std::sqrt(dx * dx + dy * dy);
        const float w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
        if (w <= 0.0f) continue;
        v.position.z += sign * b.strength * w;
        ++hits;
    }
    return hits;
}

int brushFlatten(std::vector<Vertex>& verts, const Brush& b, float targetZ) {
    int hits = 0;
    for (Vertex& v : verts) {
        const float dx = v.position.x - b.center.x;
        const float dy = v.position.y - b.center.y;
        const float d  = std::sqrt(dx * dx + dy * dy);
        const float w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
        if (w <= 0.0f) continue;
        // Move a fraction (strength*weight, capped at 1) of the way to target.
        const float a = std::clamp(b.strength * w, 0.0f, 1.0f);
        v.position.z += (targetZ - v.position.z) * a;
        ++hits;
    }
    return hits;
}

int brushRaiseLowerChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                          const Brush& b, float sign) {
    int hits = 0;
    for (MapChunk& mc : chunks) {
        forEachChunkSample(mc, blockX, blockY, [&](int m, float wx, float wy) {
            const float dx = wx - b.center.x;
            const float dy = wy - b.center.y;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
            if (w <= 0.0f) return;
            mc.heights[m] += sign * b.strength * w;   // MCVT is relative to position.z
            ++hits;
        });
    }
    return hits;
}

int brushFlattenChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                       const Brush& b, float targetZ) {
    int hits = 0;
    for (MapChunk& mc : chunks) {
        forEachChunkSample(mc, blockX, blockY, [&](int m, float wx, float wy) {
            const float dx = wx - b.center.x;
            const float dy = wy - b.center.y;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
            if (w <= 0.0f) return;
            const float a       = std::clamp(b.strength * w, 0.0f, 1.0f);
            const float worldZ  = mc.position.z + mc.heights[m];
            const float updated = worldZ + (targetZ - worldZ) * a;
            mc.heights[m]       = updated - mc.position.z;
            ++hits;
        });
    }
    return hits;
}

int paintAlpha(AlphaMap& map, float u, float v, float radius,
               float strength, Falloff falloff, uint8_t target) {
    constexpr int DIM = AlphaMap::DIM;
    int hits = 0;
    const float cx = u * DIM;            // brush centre in texel space
    const float cy = v * DIM;
    const float rad = radius * DIM;
    for (int row = 0; row < DIM; ++row) {
        for (int col = 0; col < DIM; ++col) {
            const float dx = (col + 0.5f) - cx;
            const float dy = (row + 0.5f) - cy;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float w  = falloffWeight(falloff, d, rad, 0.0f);
            if (w <= 0.0f) continue;
            uint8_t& cur = map.texels[static_cast<size_t>(row) * DIM + col];
            const float a = std::clamp(strength * w, 0.0f, 1.0f);
            const float nv = cur + (static_cast<float>(target) - cur) * a;
            cur = static_cast<uint8_t>(std::lround(std::clamp(nv, 0.0f, 255.0f)));
            ++hits;
        }
    }
    return hits;
}

} // namespace wf
