#include "terrain_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace wf {

TexMesh buildChunkTexMesh(const MapChunk& mc, int blockX, int blockY) {
    Mesh m = buildChunkMesh(mc, blockX, blockY);   // reuse the verified geometry
    TexMesh out;
    out.vertices.resize(m.vertices.size());
    // Vertex order from buildChunkMesh: outer (i,j) = i*9+j (0..80),
    // inner (i,j) = 81 + i*8 + j (81..144). UV = chunk-space (0..1).
    for (int i = 0; i < 9; ++i)
        for (int j = 0; j < 9; ++j) {
            int b = i * 9 + j;
            out.vertices[b] = { m.vertices[b].position, m.vertices[b].normal,
                                { j / 8.0f, i / 8.0f } };
        }
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            int b = 81 + i * 8 + j;
            out.vertices[b] = { m.vertices[b].position, m.vertices[b].normal,
                                { (j + 0.5f) / 8.0f, (i + 0.5f) / 8.0f } };
        }
    out.indices = m.indices;
    return out;
}

Rgba splatSample(const std::vector<TerrainLayer>& layers, float u, float v, float tiling) {
    if (layers.empty() || !layers[0].texture) return Rgba{255, 0, 255, 255};
    Rgba c = sampleTextureWrap(*layers[0].texture, u * tiling, v * tiling);
    for (size_t i = 1; i < layers.size() && i < 4; ++i) {
        const TerrainLayer& L = layers[i];
        if (!L.texture || !L.alpha) continue;
        int col = std::clamp((int)(u * (AlphaMap::DIM - 1)), 0, AlphaMap::DIM - 1);
        int row = std::clamp((int)(v * (AlphaMap::DIM - 1)), 0, AlphaMap::DIM - 1);
        float a = L.alpha->at(row, col) / 255.0f;
        if (a <= 0.0f) continue;
        Rgba t = sampleTextureWrap(*L.texture, u * tiling, v * tiling);
        c.r = (uint8_t)(c.r + (t.r - c.r) * a);
        c.g = (uint8_t)(c.g + (t.g - c.g) * a);
        c.b = (uint8_t)(c.b + (t.b - c.b) * a);
    }
    c.a = 255;
    return c;
}

namespace {
inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
inline uint8_t clamp8(float v) { return (uint8_t)std::clamp(v + 0.5f, 0.0f, 255.0f); }
} // namespace

void rasterTerrainSplat(Framebuffer& fb, const TexMesh& mesh, const Mat4& mvp,
                        const std::vector<TerrainLayer>& layers, float tiling, Vec3 lightDir) {
    int W = fb.color.width, H = fb.color.height;
    Vec3 Lr = normalize(lightDir);

    // --- TEMP DEBUG ---
    static const bool DBG = std::getenv("WF_DBG_TERRAIN") != nullptr;
    static int dbgChunk = -1;
    if (DBG) ++dbgChunk;
    long dbgPix = 0, dbgDark = 0, dbgSkipInvalid = 0, dbgBackface = 0;
    bool dbgFirstIsFallback = (!layers.empty() && layers[0].texture &&
                               layers[0].texture->width == 8 && layers[0].texture->height == 8);
    float dbgZmin = 1e30f, dbgZmax = -1e30f;
    long dbgR = 0, dbgG = 0, dbgB = 0;
    double dbgNx = 0, dbgNy = 0, dbgNz = 0; double dbgLitSum = 0; long dbgLitN = 0;
    if (DBG) {
        for (const TexVertex& vtx : mesh.vertices) {
            dbgNx += vtx.normal.x; dbgNy += vtx.normal.y; dbgNz += vtx.normal.z;
            dbgLitSum += 0.4 + 0.6 * std::max(0.0f, dot(normalize(vtx.normal), Lr));
            ++dbgLitN;
        }
    }
    // --- END TEMP DEBUG ---

    struct VOut { Vec4 clip; bool valid; };
    std::vector<VOut> vo(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vec4 c = mvp * Vec4(mesh.vertices[i].position, 1.0f);
        vo[i] = { c, c.w > 1e-4f };
    }
    auto toScreen = [&](const Vec4& c, float& sx, float& sy, float& sz, float& invw) {
        invw = 1.0f / c.w;
        sx = (c.x * invw * 0.5f + 0.5f) * W;
        sy = (1.0f - (c.y * invw * 0.5f + 0.5f)) * H;
        sz = c.z * invw;
    };

    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t+1], i2 = mesh.indices[t+2];
        if (!vo[i0].valid || !vo[i1].valid || !vo[i2].valid) { if (DBG) ++dbgSkipInvalid; continue; }
        float x0,y0,z0,iw0, x1,y1,z1,iw1, x2,y2,z2,iw2;
        toScreen(vo[i0].clip, x0,y0,z0,iw0);
        toScreen(vo[i1].clip, x1,y1,z1,iw1);
        toScreen(vo[i2].clip, x2,y2,z2,iw2);
        float area = edge(x0,y0, x1,y1, x2,y2);
        if (std::fabs(area) < 1e-6f) continue;

        const TexVertex& V0 = mesh.vertices[i0];
        const TexVertex& V1 = mesh.vertices[i1];
        const TexVertex& V2 = mesh.vertices[i2];
        int minX = std::max(0,   (int)std::floor(std::min({x0,x1,x2})));
        int maxX = std::min(W-1, (int)std::ceil (std::max({x0,x1,x2})));
        int minY = std::max(0,   (int)std::floor(std::min({y0,y1,y2})));
        int maxY = std::min(H-1, (int)std::ceil (std::max({y0,y1,y2})));

        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                float fx = px + 0.5f, fy = py + 0.5f;
                float w0 = edge(x1,y1, x2,y2, fx,fy);
                float w1 = edge(x2,y2, x0,y0, fx,fy);
                float w2 = edge(x0,y0, x1,y1, fx,fy);
                bool in = (w0>=0&&w1>=0&&w2>=0) || (w0<=0&&w1<=0&&w2<=0);
                if (!in) continue;
                float l0 = w0/area, l1 = w1/area, l2 = w2/area;
                float z = l0*z0 + l1*z1 + l2*z2;
                float& dref = fb.depth[(size_t)py*W + px];
                if (z >= dref) continue;

                float iw = l0*iw0 + l1*iw1 + l2*iw2;
                float u = (l0*V0.uv.x*iw0 + l1*V1.uv.x*iw1 + l2*V2.uv.x*iw2) / iw;
                float v = (l0*V0.uv.y*iw0 + l1*V1.uv.y*iw1 + l2*V2.uv.y*iw2) / iw;
                Vec3 n = (V0.normal*(l0*iw0) + V1.normal*(l1*iw1) + V2.normal*(l2*iw2)) * (1.0f/iw);
                n = normalize(n);
                float light = 0.4f + 0.6f * std::max(0.0f, dot(n, Lr));

                Rgba c = splatSample(layers, u, v, tiling);
                c.r = clamp8(c.r * light); c.g = clamp8(c.g * light); c.b = clamp8(c.b * light);
                c.a = 255;
                dref = z;
                fb.color.at(px, py) = c;
                if (DBG) {
                    ++dbgPix; dbgR += c.r; dbgG += c.g; dbgB += c.b;
                    if ((int)c.r + c.g + c.b < 90) ++dbgDark;
                    dbgZmin = std::min(dbgZmin, z); dbgZmax = std::max(dbgZmax, z);
                }
            }
        }
    }
    if (DBG && dbgPix > 0) {
        long darkPct = dbgDark * 100 / dbgPix;
        if (darkPct > 30 || dbgFirstIsFallback || dbgChunk < 2 || dbgChunk == 100) {
            std::fprintf(stderr,
                "DBG chunk=%d layers=%zu fallbackBase=%d pix=%ld dark%%=%ld "
                "avgRGB=(%ld,%ld,%ld) z=[%.4f,%.4f] avgN=(%.2f,%.2f,%.2f) avgLit=%.2f skipInvalid=%ld\n",
                dbgChunk, layers.size(), (int)dbgFirstIsFallback, dbgPix, darkPct,
                dbgR/dbgPix, dbgG/dbgPix, dbgB/dbgPix, dbgZmin, dbgZmax,
                dbgNx/dbgLitN, dbgNy/dbgLitN, dbgNz/dbgLitN, dbgLitSum/dbgLitN, dbgSkipInvalid);
        }
    }
    (void)dbgBackface;
}

} // namespace wf
