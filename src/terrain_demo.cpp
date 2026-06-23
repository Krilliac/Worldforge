// Terrain texturing demo: build a patch of MCNK chunks with procedural heights,
// splat a grass base + an alpha-mapped rock layer (the MCAL multi-layer blend),
// and render textured terrain -> PNG. The biggest visual jump from height-shaded
// to real WoW terrain; the alpha maps here are synthetic (height-driven), the
// real path feeds decodeAlphaMap output into the same TerrainLayer list.
#include <cmath>
#include <cstdio>
#include <vector>

#include "terrain.hpp"
#include "terrain_render.hpp"
#include "coords.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"

using namespace wf;

static float heightAt(float x, float y) {
    return 18.0f * std::sin(x * 0.012f) * std::cos(y * 0.013f)
         + 7.0f  * std::sin((x + y) * 0.03f);
}

// A small procedural tile texture with per-texel noise so tiling reads.
static Image noiseTex(Rgba base, int amp) {
    Image t(32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            int n = ((x * 53 + y * 97) % (2 * amp + 1)) - amp;
            t.at(x, y) = Rgba{
                (uint8_t)std::min(255, std::max(0, base.r + n)),
                (uint8_t)std::min(255, std::max(0, base.g + n)),
                (uint8_t)std::min(255, std::max(0, base.b + n)), 255 };
        }
    return t;
}

int main() {
    const int W = 960, H = 600;
    Framebuffer fb(W, H);
    fb.clear(Rgba{ 20, 24, 34, 255 });

    Image grass = noiseTex(Rgba{ 60, 130, 50, 255 }, 18);
    Image rock  = noiseTex(Rgba{ 130, 125, 120, 255 }, 20);

    const int CHUNKS = 5;
    const float U = (float)UNIT_SIZE;
    const int blockX = 32, blockY = 32;

    std::vector<TexMesh>  meshes;        // keep alive while rendering
    std::vector<AlphaMap> alphas;
    meshes.reserve(CHUNKS * CHUNKS);
    alphas.reserve(CHUNKS * CHUNKS);

    for (int iy = 0; iy < CHUNKS; ++iy) {
        for (int ix = 0; ix < CHUNKS; ++ix) {
            MapChunk mc;
            mc.indexX = ix; mc.indexY = iy;
            mc.position = { 0, 0, 0 };
            Vec3 corner = chunkCornerWorld(blockX, blockY, iy, ix, 0.0f);

            auto setH = [&](int m, float wx, float wy) {
                mc.heights[m] = heightAt(wx, wy);
                float hx = heightAt(wx + U, wy) - heightAt(wx - U, wy);
                float hy = heightAt(wx, wy + U) - heightAt(wx, wy - U);
                mc.normals[m] = normalize(Vec3{ -hx, -hy, 2 * U });
            };
            for (int i = 0; i < 9; ++i)
                for (int j = 0; j < 9; ++j)
                    setH(i*17 + j, corner.x - i*U, corner.y - j*U);
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j)
                    setH(i*17 + 9 + j, corner.x - (i+0.5f)*U, corner.y - (j+0.5f)*U);

            // Rock coverage: more rock the higher the ground (height-driven).
            AlphaMap am;
            for (int row = 0; row < AlphaMap::DIM; ++row)
                for (int col = 0; col < AlphaMap::DIM; ++col) {
                    float wx = corner.x - (row / 63.0f) * (8 * U);
                    float wy = corner.y - (col / 63.0f) * (8 * U);
                    float h = heightAt(wx, wy);
                    // Rock on the higher ground + a slope term so ridges show.
                    float slope = std::fabs(heightAt(wx + U, wy) - heightAt(wx - U, wy)) * 0.25f;
                    float cov = std::min(1.0f, std::max(0.0f, (h + 6.0f) / 16.0f + slope));
                    am.texels[row * AlphaMap::DIM + col] = (uint8_t)(cov * 255);
                }
            alphas.push_back(am);
            meshes.push_back(buildChunkTexMesh(mc, blockX, blockY));
        }
    }

    // World centre of the patch (corners run negative from the tile origin).
    float ext = CHUNKS * 8 * U * 0.5f;
    Vec3 center{ -ext, -ext, 4.0f };
    Mat4 view = Mat4::lookAt(center + Vec3{ ext*1.4f, ext*1.4f, ext*1.3f }, center, {0,0,1});
    Mat4 proj = Mat4::perspective(55.0, double(W)/H, 1.0, 4000.0);
    Mat4 mvp = proj * view;

    for (size_t k = 0; k < meshes.size(); ++k) {
        std::vector<TerrainLayer> layers = { { &grass, nullptr }, { &rock, &alphas[k] } };
        rasterTerrainSplat(fb, meshes[k], mvp, layers, 6.0f, Vec3{ 0.5f, 0.4f, 0.8f });
    }

    const char* out = "worldforge_terrain_tex.png";
    if (!writePng(fb.color, out)) { std::fprintf(stderr, "write failed\n"); return 1; }
    std::printf("wrote %s  (%dx%d chunks, grass+rock MCAL splat)\n", out, CHUNKS, CHUNKS);
    return 0;
}
