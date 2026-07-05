// ---------------------------------------------------------------------------
// Headless minimap generator tests: a tiny synthetic tile (flat red base
// chunk + a tilted "hill" chunk + a water chunk + one doodad) rendered
// top-down through renderMinimapTile, the md5translate WRITER round-tripped
// through the EXISTING minimap.cpp reader (the oracle), and the whole-map
// stitcher. Everything is pure software rasteriser -- no GPU, no files.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "minimap_gen.hpp"

#include "coords.hpp"
#include "image.hpp"
#include "math.hpp"
#include "minimap.hpp"
#include "terrain.hpp"
#include "terrain_render.hpp"

#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace wf;

namespace {

// The synthetic tile lives at ADT block (32, 48) (matching the map32_48
// naming used in the .trs test). Chunk grid cells used, all in row 0:
//   col 0 = flat red land, col 1 = tilted grey "hill" slope,
//   col 2 = flat red land UNDER water, col 3 = flat grey reference ground.
constexpr int kBX = 32, kBY = 48;

// Add one solid-coloured chunk at grid (col, row). The 1x1 base texture is
// kept alive in tr.textures (the shared_ptr keep-alive slot), and `water`
// adds an MCLQ river surface 2 yd above the ground.
void addChunk(TileRender& tr, int col, int row, Rgba base, float height,
              Vec3 normal, bool water = false) {
    MapChunk mc;
    mc.indexX   = static_cast<uint32_t>(col);
    mc.indexY   = static_cast<uint32_t>(row);
    mc.position = { 0, 0, 0 };
    for (float& h : mc.heights) h = height;
    for (Vec3& n : mc.normals)  n = normalize(normal);

    tr.chunkMeshes.push_back(buildChunkTexMesh(mc, kBX, kBY));
    auto tex = std::make_shared<Image>(1, 1);
    tex->at(0, 0) = base;
    tr.textures.push_back(tex);
    tr.chunkAlphas.emplace_back();                              // base layer only
    tr.chunkLayers.push_back({ TerrainLayer{ tex.get(), nullptr } });
    tr.chunkShadows.emplace_back();                             // no MCSH

    if (water) {
        mc.hasLiquid        = true;
        mc.liquidType       = LiquidType::River;
        mc.liquid.type      = LiquidType::River;
        mc.liquid.minHeight = height + 2.0f;
        mc.liquid.maxHeight = height + 2.0f;
        mc.liquid.heights.fill(height + 2.0f);
        mc.liquid.renderFlags.fill(0);                          // every cell renders
        tr.liquids.push_back({ buildLiquidMesh(mc, kBX, kBY),
                               liquidTint(LiquidType::River), false,
                               LiquidType::River });
    }
}

// Build the synthetic streamed tile: 4 terrain chunks + one bright doodad
// quad hovering over chunk (col 4, row 4).
TileScene makeScene() {
    TileScene ts;
    const Rgba red { 180,  60,  40, 255 };
    const Rgba grey{ 150, 150, 150, 255 };
    addChunk(ts.terrain, 0, 0, red,   0.0f, { 0, 0, 1 });
    addChunk(ts.terrain, 1, 0, grey, 10.0f, { -0.6f, 0, 0.8f });   // south-facing slope
    addChunk(ts.terrain, 2, 0, red,   0.0f, { 0, 0, 1 }, /*water=*/true);
    addChunk(ts.terrain, 3, 0, grey,  0.0f, { 0, 0, 1 });

    // One M2 doodad: a yellow quad, +-8 yd, 15 yd up, centred on chunk (4,4).
    const float C = static_cast<float>(CHUNK_SIZE);
    Vec3 corner   = chunkCornerWorld(kBX, kBY, 4, 4, 0.0f);
    Vec3 centre   = { corner.x - C * 0.5f, corner.y - C * 0.5f, 15.0f };
    TexMesh quad;
    quad.vertices = {
        { { -8, -8, 0 }, { 0, 0, 1 }, { 0, 0 } },
        { {  8, -8, 0 }, { 0, 0, 1 }, { 1, 0 } },
        { {  8,  8, 0 }, { 0, 0, 1 }, { 1, 1 } },
        { { -8,  8, 0 }, { 0, 0, 1 }, { 0, 1 } },
    };
    quad.indices = { 0, 1, 2, 0, 2, 3 };
    ts.meshes.push_back(quad);
    auto tex = std::make_shared<Image>(1, 1);
    tex->at(0, 0) = Rgba{ 255, 240, 40, 255 };
    ts.textures.push_back(tex);
    ts.instances.push_back({ 0, 0, Mat4::translate(centre) });
    return ts;
}

bool samePixel(const Rgba& a, const Rgba& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
int lum(const Rgba& p) { return int(p.r) + p.g + p.b; }

// Count pixels that differ between two same-sized images.
int diffCount(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height) return -1;
    int n = 0;
    for (size_t i = 0; i < a.pixels.size(); ++i)
        if (!samePixel(a.pixels[i], b.pixels[i])) ++n;
    return n;
}

}  // namespace

void test_minimap_gen() {
    std::printf("[minimap_gen]\n");

    TileScene scene = makeScene();

    // At 128 px the 16x16 chunk grid is 8 px per cell; probe cell centres in
    // row 0: land (4,4), hill (12,4), water (20,4), flat grey (28,4).
    MinimapRenderSettings s;
    s.resolution = 128;

    // --- (1) render: dimensions, non-empty, water toggle ------------------
    {
        Image on = renderMinimapTile(scene, kBX, kBY, s);
        CHECK(on.width == 128 && on.height == 128);
        int covered = 0;
        for (const Rgba& p : on.pixels) if (p.a == 255) ++covered;
        CHECK(covered > 4 * 8 * 8 / 2);            // the 4 chunk cells drew
        // Uncovered background stays fully transparent (stitch-friendly).
        CHECK(on.at(64, 64).a == 0);
        // Land pixel is red-ish (the base layer texture, lit).
        CHECK(on.at(4, 4).r > on.at(4, 4).g);

        MinimapRenderSettings dry = s;
        dry.drawWater = false;
        Image off = renderMinimapTile(scene, kBX, kBY, dry);
        CHECK(!samePixel(on.at(20, 4), off.at(20, 4)));   // water tints its cell
        CHECK(samePixel(on.at(4, 4), off.at(4, 4)));      // dry land unaffected
    }

    // --- (2) elevation shading: slope N.L darker than flat ----------------
    {
        Image lit = renderMinimapTile(scene, kBX, kBY, s);
        CHECK(lum(lit.at(12, 4)) < lum(lit.at(28, 4)));   // hill slope darker

        MinimapRenderSettings flat = s;
        flat.elevationShading = false;                    // unlit: texel as-is
        Image un = renderMinimapTile(scene, kBX, kBY, flat);
        CHECK(samePixel(un.at(12, 4), un.at(28, 4)));     // same grey texture
    }

    // --- (3) determinism: two renders are byte-identical ------------------
    {
        Image a = renderMinimapTile(scene, kBX, kBY, s);
        Image b = renderMinimapTile(scene, kBX, kBY, s);
        CHECK(diffCount(a, b) == 0);
    }

    // --- resolution edge cases: any >= 64, smaller clamps up --------------
    {
        MinimapRenderSettings odd = s;
        odd.resolution = 100;                             // not a power of two
        Image i100 = renderMinimapTile(scene, kBX, kBY, odd);
        CHECK(i100.width == 100 && i100.height == 100);
        odd.resolution = 16;
        Image i64 = renderMinimapTile(scene, kBX, kBY, odd);
        CHECK(i64.width == 64 && i64.height == 64);
    }

    // --- (4) .trs writer round-trips through the minimap.cpp reader -------
    {
        TrsEntry e = makeTrsEntry("Kalimdor", 32, 48);
        CHECK(e.plainName == MinimapIndex::tileKey("Kalimdor", 32, 48));
        CHECK(e.hashedName.size() == 36);                 // 32 hex chars + ".blp"
        CHECK(e.hashedName.substr(32) == ".blp");
        // Stable and unique: same tile -> same name, other tile -> another.
        CHECK(makeTrsEntry("Kalimdor", 32, 48).hashedName == e.hashedName);
        CHECK(makeTrsEntry("Kalimdor", 32, 49).hashedName != e.hashedName);

        std::string trs = buildMd5Translate("Kalimdor", { e });
        MinimapIndex idx = MinimapIndex::parse(trs);      // the EXISTING reader
        CHECK(idx.size() == 1);
        CHECK(idx.tile("Kalimdor", 32, 48) == e.hashedName);

        // A bare left column is prefixed with the map dir by the writer.
        std::string bare = buildMd5Translate("Emerald",
                                             { { "map03_04.blp", "aa.blp" } });
        CHECK(MinimapIndex::parse(bare).tile("Emerald", 3, 4) == "aa.blp");
    }

    // --- (5) stitch: tile (1,0) lands at pixel offset (256,0) -------------
    {
        Image ga(4, 4), mb(4, 4), bc(4, 4);
        for (Rgba& p : ga.pixels) p = Rgba{   0, 255,   0, 255 };
        for (Rgba& p : mb.pixels) p = Rgba{ 255,   0, 255, 255 };
        for (Rgba& p : bc.pixels) p = Rgba{   0,   0, 255, 255 };
        std::map<std::pair<int, int>, Image> tiles;
        tiles[{ 0, 0 }] = ga;
        tiles[{ 1, 0 }] = mb;                             // one column east
        tiles[{ 0, 1 }] = bc;                             // one row south
        Image big = stitchMinimap(tiles, 256);
        CHECK(big.width == 512 && big.height == 512);
        CHECK(samePixel(big.at(260, 5),   Rgba{ 255, 0, 255, 255 }));  // (1,0) marker
        CHECK(samePixel(big.at(5, 5),     Rgba{ 0, 255, 0, 255 }));
        CHECK(samePixel(big.at(5, 260),   Rgba{ 0, 0, 255, 255 }));
        CHECK(big.at(260, 260).a == 0);                   // missing (1,1): transparent
        CHECK(stitchMinimap({}, 256).width == 0);         // empty in, empty out
    }

    // --- (6) drawM2 toggle changes the framebuffer ------------------------
    {
        Image with = renderMinimapTile(scene, kBX, kBY, s);
        MinimapRenderSettings noM2 = s;
        noM2.drawM2 = false;
        Image without = renderMinimapTile(scene, kBX, kBY, noM2);
        CHECK(diffCount(with, without) > 0);              // the doodad drew
    }
}
