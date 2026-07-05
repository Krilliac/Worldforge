#include "editing.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

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

// True when `mode` lets the flatten tool move a height `z` toward `target`.
bool flattenModeAllows(FlattenMode mode, float z, float target) {
    switch (mode) {
        case FlattenMode::Both:      return true;
        case FlattenMode::RaiseOnly: return z < target;   // only lift from below
        case FlattenMode::LowerOnly: return z > target;   // only drop from above
    }
    return false;
}

// xorshift32: tiny explicit PRNG so brush scatter is exactly reproducible.
// 0 is the sequence's fixed point, so remap it to an arbitrary nonzero seed.
uint32_t xorshift32(uint32_t& state) {
    if (state == 0) state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Next uniform float in [0,1) from the PRNG (24 mantissa-safe bits).
float nextUnit(uint32_t& state) {
    return static_cast<float>(xorshift32(state) >> 8) * (1.0f / 16777216.0f);
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
        case Falloff::Polynomial:                     // cubic ease, 1 -> 0
            return 1.0f - t * t * t;
        case Falloff::Trigonometric:                  // cosine quarter-wave
            // max() guards the t~1 rounding case from dipping below zero.
            return std::max(0.0f, std::cos(t * static_cast<float>(kPi) * 0.5f));
        case Falloff::Quadratic:                      // parabolic, 1 -> 0
            return 1.0f - t * t;
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

float flattenPlaneTarget(const FlattenPlane& plane, float worldX, float worldY) {
    float o = std::fmod(plane.orientationDeg, 360.0f);        // wrap to [0,360)
    if (o < 0.0f) o += 360.0f;
    const float ang   = std::clamp(plane.angleDeg, 0.0f, 89.0f);
    const float slope = static_cast<float>(std::tan(radians(ang)));
    const float dirX  = static_cast<float>(std::cos(radians(o)));
    const float dirY  = static_cast<float>(std::sin(radians(o)));
    const float along = (worldX - plane.lock.x) * dirX + (worldY - plane.lock.y) * dirY;
    return plane.lock.z + slope * along;
}

int brushFlatten(std::vector<Vertex>& verts, const Brush& b,
                 const FlattenPlane& plane, FlattenMode mode) {
    int hits = 0;
    for (Vertex& v : verts) {
        const float dx = v.position.x - b.center.x;
        const float dy = v.position.y - b.center.y;
        const float d  = std::sqrt(dx * dx + dy * dy);
        const float w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
        if (w <= 0.0f) continue;
        const float target = flattenPlaneTarget(plane, v.position.x, v.position.y);
        if (!flattenModeAllows(mode, v.position.z, target)) continue;
        // Move a fraction (strength*weight, capped at 1) of the way to the
        // plane -- so the vertex converges on it and never overshoots.
        const float a = std::clamp(b.strength * w, 0.0f, 1.0f);
        v.position.z += (target - v.position.z) * a;
        ++hits;
    }
    return hits;
}

int brushFlattenChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                       const Brush& b, const FlattenPlane& plane, FlattenMode mode) {
    int hits = 0;
    for (MapChunk& mc : chunks) {
        forEachChunkSample(mc, blockX, blockY, [&](int m, float wx, float wy) {
            const float dx = wx - b.center.x;
            const float dy = wy - b.center.y;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
            if (w <= 0.0f) return;
            const float target = flattenPlaneTarget(plane, wx, wy);
            const float worldZ = mc.position.z + mc.heights[m];
            if (!flattenModeAllows(mode, worldZ, target)) return;
            const float a       = std::clamp(b.strength * w, 0.0f, 1.0f);
            const float updated = worldZ + (target - worldZ) * a;
            mc.heights[m]       = updated - mc.position.z;
            ++hits;
        });
    }
    return hits;
}

int brushSmoothChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                      const Brush& b) {
    if (b.radius <= 0.0f || b.strength <= 0.0f) return 0;

    // MCVT samples live on a half-UNIT lattice (inner verts sit half a cell in
    // from the outer grid). Group samples by their integer lattice coordinate:
    // border samples duplicated in adjacent chunks land in the same group, so
    // they share one mean, one result, and the seam stays closed bit-exactly.
    const double H = UNIT_SIZE * 0.5;                 // lattice pitch, ~2.083 yd
    struct Group {
        long long gx = 0, gy = 0;                     // lattice coordinate
        double    zSum   = 0.0;                       // world-Z accumulator
        int       zCount = 0;
        std::vector<std::pair<int, int>> refs;        // (chunk index, MCVT index)
    };
    const auto keyOf = [](long long gx, long long gy) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(gx)) << 32) |
                static_cast<uint64_t>(static_cast<uint32_t>(gy));
    };
    std::vector<Group> groups;
    std::unordered_map<uint64_t, size_t> index;
    for (int c = 0; c < static_cast<int>(chunks.size()); ++c) {
        const MapChunk& mc = chunks[c];
        forEachChunkSample(mc, blockX, blockY, [&](int m, float wx, float wy) {
            const long long gx = std::llround(wx / H);
            const long long gy = std::llround(wy / H);
            const auto [it, added] = index.try_emplace(keyOf(gx, gy), groups.size());
            if (added) { groups.emplace_back(); groups.back().gx = gx; groups.back().gy = gy; }
            Group& g = groups[it->second];
            g.zSum += mc.position.z + mc.heights[m];
            ++g.zCount;
            g.refs.emplace_back(c, m);
        });
    }

    // Snapshot every group's height first (Jacobi step): targets are computed
    // from the pre-pass field, so the result is order-independent.
    std::vector<float> z0(groups.size());
    for (size_t i = 0; i < groups.size(); ++i)
        z0[i] = static_cast<float>(groups[i].zSum / groups[i].zCount);

    int hits = 0;
    for (size_t i = 0; i < groups.size(); ++i) {
        const Group& g  = groups[i];
        const float  wx = static_cast<float>(g.gx * H);   // canonical world XY,
        const float  wy = static_cast<float>(g.gy * H);   // identical for duplicates
        const float  dx = wx - b.center.x;
        const float  dy = wy - b.center.y;
        const float  d  = std::sqrt(dx * dx + dy * dy);
        const float  w  = falloffWeight(b.falloff, d, b.radius, b.innerRatio);
        if (w <= 0.0f) continue;
        // Neighbour mean over lattice offsets within 1.5 sample spacings
        // (3 half-steps): di^2+dj^2 <= 9. Mixed-parity offsets simply find no
        // sample there and drop out of the mean.
        double nSum = 0.0;
        int    nCount = 0;
        for (int di = -3; di <= 3; ++di) {
            for (int dj = -3; dj <= 3; ++dj) {
                if ((di == 0 && dj == 0) || di * di + dj * dj > 9) continue;
                const auto it = index.find(keyOf(g.gx + di, g.gy + dj));
                if (it == index.end()) continue;
                nSum += z0[it->second];
                ++nCount;
            }
        }
        if (nCount == 0) continue;
        const float mean = static_cast<float>(nSum / nCount);
        const float a    = std::clamp(b.strength * w, 0.0f, 1.0f);
        const float newZ = z0[i] + (mean - z0[i]) * a;
        for (const auto& [c, m] : g.refs) {               // identical write per duplicate
            chunks[c].heights[m] = newZ - chunks[c].position.z;
            ++hits;
        }
    }
    return hits;
}

int paintAlpha(AlphaMap& map, float u, float v, float radius,
               float strength, Falloff falloff, uint8_t target) {
    return paintAlpha(map, u, v, radius, 0.0f, strength, falloff, target);
}

int paintAlpha(AlphaMap& map, float u, float v, float radius,
               float hardness, float pressure, Falloff falloff, uint8_t target) {
    constexpr int DIM = AlphaMap::DIM;
    int hits = 0;
    const float cx = u * DIM;            // brush centre in texel space
    const float cy = v * DIM;
    const float rad  = radius * DIM;
    const float hard = std::clamp(hardness, 0.0f, 1.0f);   // -> inner ratio
    for (int row = 0; row < DIM; ++row) {
        for (int col = 0; col < DIM; ++col) {
            const float dx = (col + 0.5f) - cx;
            const float dy = (row + 0.5f) - cy;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float w  = falloffWeight(falloff, d, rad, hard);
            if (w <= 0.0f) continue;
            uint8_t& cur = map.texels[static_cast<size_t>(row) * DIM + col];
            const float a = std::clamp(pressure * w, 0.0f, 1.0f);
            const float nv = cur + (static_cast<float>(target) - cur) * a;
            cur = static_cast<uint8_t>(std::lround(std::clamp(nv, 0.0f, 255.0f)));
            ++hits;
        }
    }
    return hits;
}

int sprayAlpha(AlphaMap& map, float u, float v, float outerRadius,
               float dabRadius, int dabsPerStep, uint32_t& rngState,
               float pressure, Falloff falloff, uint8_t target) {
    if (outerRadius <= 0.0f || dabRadius <= 0.0f || dabsPerStep <= 0) return 0;
    int hits = 0;
    for (int dab = 0; dab < dabsPerStep; ++dab) {
        // Rejection-sample the unit disc for a uniform in-disc offset.
        float ox, oy;
        do {
            ox = nextUnit(rngState) * 2.0f - 1.0f;
            oy = nextUnit(rngState) * 2.0f - 1.0f;
        } while (ox * ox + oy * oy > 1.0f);
        hits += paintAlpha(map, u + ox * outerRadius, v + oy * outerRadius,
                           dabRadius, 0.0f, pressure, falloff, target);
    }
    return hits;
}

float imageBrushWeight(const ImageBrush& ib, float dx, float dy, float radius) {
    if (radius <= 0.0f || ib.pixels == nullptr || ib.width < 1 || ib.height < 1)
        return 0.0f;
    // Undo the brush rotation, then map [-radius, radius] -> [0,1] UV.
    const float a  = static_cast<float>(radians(-ib.rotationDeg));
    const float c  = std::cos(a);
    const float s  = std::sin(a);
    const float rx = dx * c - dy * s;
    const float ry = dx * s + dy * c;
    const float u  = (rx / radius + 1.0f) * 0.5f;
    const float v  = (ry / radius + 1.0f) * 0.5f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 0.0f;
    // Bilinear sample with texel centres at the grid nodes; a 1x1 image has a
    // single node everywhere and so degenerates to a flat brush.
    const float px = u * (ib.width - 1);
    const float py = v * (ib.height - 1);
    const int   x0 = static_cast<int>(px);
    const int   y0 = static_cast<int>(py);
    const int   x1 = std::min(x0 + 1, ib.width - 1);
    const int   y1 = std::min(y0 + 1, ib.height - 1);
    const float fx = px - x0;
    const float fy = py - y0;
    const auto  at = [&](int x, int y) {
        return static_cast<float>(ib.pixels[static_cast<size_t>(y) * ib.width + x]);
    };
    const float top    = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return (top + (bottom - top) * fy) * (1.0f / 255.0f);
}

int brushStampChunks(std::vector<MapChunk>& chunks, int blockX, int blockY,
                     Vec3 center, float radius, float strength, float sign,
                     const ImageBrush& ib) {
    if (radius <= 0.0f) return 0;
    int hits = 0;
    for (MapChunk& mc : chunks) {
        forEachChunkSample(mc, blockX, blockY, [&](int m, float wx, float wy) {
            const float w = imageBrushWeight(ib, wx - center.x, wy - center.y, radius);
            if (w <= 0.0f) return;
            mc.heights[m] += sign * strength * w;     // MCVT is relative to position.z
            ++hits;
        });
    }
    return hits;
}

} // namespace wf
