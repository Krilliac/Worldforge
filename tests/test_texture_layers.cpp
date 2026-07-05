#include "test.hpp"
#include "texture_layers.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace wf;

// A chunk blending the given MTEX indices: layer 0 is the opaque base coat,
// later layers carry MCLY_USE_ALPHA. The MCAL blob starts empty (decodes to
// transparent), as after parsing a freshly themed chunk.
static MapChunk makeChunk(std::initializer_list<uint32_t> texIds) {
    MapChunk mc;
    for (uint32_t id : texIds) {
        TexLayer layer;
        layer.textureId = id;
        layer.flags     = mc.layers.empty() ? 0u : MCLY_USE_ALPHA;
        layer.effectId  = MCLY_NO_EFFECT;
        mc.layers.push_back(layer);
    }
    return mc;
}

// A 64x64 coverage map filled with one value.
static AlphaMap filled(uint8_t v) {
    AlphaMap m;
    m.texels.fill(v);
    return m;
}

void test_texture_layers() {
    std::printf("[texture_layers]\n");
    using Err = LayerEditResult::Err;

    // --- ensureLayer: create, then find --------------------------------------
    {
        MapChunk mc = makeChunk({0});                 // base coat only
        std::vector<AlphaMap> alphas(1);
        LayerEditResult r = ensureLayer(mc, alphas, 5);
        CHECK(r.err == Err::None);
        CHECK(r.created);
        CHECK(r.layerIndex == 1);
        CHECK(mc.layers.size() == 2);
        CHECK(alphas.size() == 2);
        CHECK(mc.layers[1].textureId == 5);
        CHECK(mc.layers[1].flags == MCLY_USE_ALPHA);  // 0x100
        CHECK(mc.layers[1].effectId == 0xFFFFFFFFu);  // no ground effect
        bool zeroed = true;
        for (uint8_t t : alphas[1].texels) if (t != 0) { zeroed = false; break; }
        CHECK(zeroed);                                // new map fully transparent

        // Second call for the same texture: found, not created, same index.
        LayerEditResult r2 = ensureLayer(mc, alphas, 5);
        CHECK(r2.err == Err::None);
        CHECK(!r2.created);
        CHECK(r2.layerIndex == 1);
        CHECK(mc.layers.size() == 2);
        CHECK(alphas.size() == 2);
    }

    // --- ensureLayer: creating on an empty chunk makes the base coat ---------
    {
        MapChunk mc;
        std::vector<AlphaMap> alphas;
        LayerEditResult r = ensureLayer(mc, alphas, 7);
        CHECK(r.created && r.layerIndex == 0);
        CHECK((mc.layers[0].flags & MCLY_USE_ALPHA) == 0);   // base: no alpha map
        CHECK(mc.layers[0].effectId == 0xFFFFFFFFu);
    }

    // --- ensureLayer: 4-layer cap ---------------------------------------------
    {
        MapChunk mc = makeChunk({0, 1, 2, 3});
        std::vector<AlphaMap> alphas(4);
        LayerEditResult r = ensureLayer(mc, alphas, 9);      // would be the 5th
        CHECK(r.err == Err::LayerCapReached);
        CHECK(!r.created);
        CHECK(r.layerIndex == -1);
        CHECK(mc.layers.size() == 4);                        // nothing mutated
        CHECK(alphas.size() == 4);
        for (size_t i = 0; i < 4; ++i) CHECK(mc.layers[i].textureId == i);
        // A texture the full chunk ALREADY blends is still found.
        LayerEditResult r2 = ensureLayer(mc, alphas, 2);
        CHECK(r2.err == Err::None && !r2.created && r2.layerIndex == 2);
    }

    // --- ensureLayer: tolerate an empty MCAL blob with layers > 1 ------------
    {
        MapChunk mc = makeChunk({0, 1});                     // alpha blob empty
        std::vector<AlphaMap> alphas;                        // nothing decoded yet
        LayerEditResult r = ensureLayer(mc, alphas, 4);
        CHECK(r.created && r.layerIndex == 2);
        CHECK(alphas.size() == 3);                           // padded + appended
        bool padZeroed = true;
        for (uint8_t t : alphas[1].texels) if (t != 0) { padZeroed = false; break; }
        CHECK(padZeroed);                                    // pad decodes-as-zeros
    }

    // --- ensureMtexEntry: case-insensitive find-or-append --------------------
    {
        std::vector<std::string> mtex = {"Tileset\\Elwynn\\grass.blp"};
        CHECK(ensureMtexEntry(mtex, "TILESET\\ELWYNN\\GRASS.BLP") == 0);
        CHECK(mtex.size() == 1);                             // found, not appended
        CHECK(ensureMtexEntry(mtex, "Tileset\\Elwynn\\snow.blp") == 1);
        CHECK(mtex.size() == 2);
        CHECK(mtex[1] == "Tileset\\Elwynn\\snow.blp");       // appended verbatim
        CHECK(ensureMtexEntry(mtex, "tileset\\elwynn\\SNOW.blp") == 1);
    }

    // --- normalizeLayers: over-full texels rescale, edited layer untouched ---
    {
        // Base + three alpha layers. Texel 0 over-full, texel 1 already fine.
        std::vector<AlphaMap> alphas(4);
        alphas[1].texels[0] = 200;   // the fresh stroke (edited layer)
        alphas[2].texels[0] = 100;
        alphas[3].texels[0] = 60;    // sum 360 > 255
        alphas[1].texels[1] = 10;
        alphas[2].texels[1] = 20;
        alphas[3].texels[1] = 30;    // sum 60: untouched
        normalizeLayers(alphas, 1);
        CHECK(alphas[1].texels[0] == 200);                   // edited preserved
        // Others scaled by (255-200)/(360-200) = 55/160, rounded.
        CHECK(alphas[2].texels[0] == 34);                    // round(34.375)
        CHECK(alphas[3].texels[0] == 21);                    // round(20.625)
        CHECK(alphas[1].texels[0] + alphas[2].texels[0] + alphas[3].texels[0] <= 255);
        CHECK(alphas[2].texels[1] == 20 && alphas[3].texels[1] == 30);

        // Whole-map property: after normalising, every texel sum is <= 255.
        std::vector<AlphaMap> full(4);
        full[1] = filled(255);       // edited layer painted to full coverage
        full[2] = filled(255);
        full[3] = filled(128);
        normalizeLayers(full, 1);
        bool editedIntact = true, sumsOk = true;
        for (size_t t = 0; t < full[1].texels.size(); ++t) {
            if (full[1].texels[t] != 255) editedIntact = false;
            if (full[1].texels[t] + full[2].texels[t] + full[3].texels[t] > 255)
                sumsOk = false;
        }
        CHECK(editedIntact);                                 // edited preserved
        CHECK(sumsOk);                                       // every texel <= 255
    }

    // --- removeLayer ----------------------------------------------------------
    {
        MapChunk mc = makeChunk({0, 1, 2});
        std::vector<AlphaMap> alphas(3);
        alphas[2] = filled(85);
        CHECK(removeLayer(mc, alphas, 0) == -1);             // base not removable
        CHECK(removeLayer(mc, alphas, 3) == -1);             // out of range
        CHECK(mc.layers.size() == 3);
        CHECK(removeLayer(mc, alphas, 1) == 2);              // new layer count
        CHECK(mc.layers.size() == 2 && alphas.size() == 2);
        CHECK(mc.layers[1].textureId == 2);                  // old layer 2 shifted
        CHECK(alphas[1].texels[0] == 85);                    // its map moved with it
    }

    // --- swapTexture(Tile): re-theme with byte-identical alphas --------------
    {
        std::vector<std::string> mtex = {
            "Tileset\\Elwynn\\grass.blp",     // 0: layer-0 base everywhere
            "Tileset\\Elwynn\\dirt.blp",      // 1
            "Tileset\\Elwynn\\rock.blp",      // 2
        };
        std::vector<MapChunk> chunks = { makeChunk({0, 1, 2}), makeChunk({0, 2}) };
        // Give both chunks real MCAL data (multiples of 17 encode exactly).
        std::vector<AlphaMap> a0 = { AlphaMap{}, filled(170), filled(85) };
        std::vector<AlphaMap> a1 = { AlphaMap{}, filled(51) };
        commitAlphas(chunks[0], a0, false);
        commitAlphas(chunks[1], a1, false);
        const std::vector<uint8_t> blob0 = chunks[0].alpha;
        const std::vector<uint8_t> blob1 = chunks[1].alpha;

        // Swap the BASE texture (layer 0) tile-wide: allowed, just retargets.
        int n = swapTexture(mtex, chunks, "TILESET\\elwynn\\GRASS.blp",
                            "Tileset\\Winterspring\\snow.blp", SwapScope::Tile);
        CHECK(n == 2);                                       // one layer per chunk
        CHECK(mtex.size() == 4);                             // snow appended
        CHECK(mtex[3] == "Tileset\\Winterspring\\snow.blp");
        CHECK(chunks[0].layers[0].textureId == 3);
        CHECK(chunks[1].layers[0].textureId == 3);
        CHECK(chunks[0].layers[1].textureId == 1);           // others untouched
        CHECK(chunks[0].layers[2].textureId == 2);
        // NO alpha data moved: raw blobs byte-identical...
        CHECK(chunks[0].alpha == blob0);
        CHECK(chunks[1].alpha == blob1);
        // ...and every decoded AlphaMap matches its pre-swap counterpart
        // (layer 0 has no stored map; it decodes to the implicit opaque 255s).
        for (size_t i = 1; i < chunks[0].layers.size(); ++i) {
            AlphaMap post = decodeAlphaMap(chunks[0], i, false);
            CHECK(std::memcmp(post.texels.data(), a0[i].texels.data(),
                              post.texels.size()) == 0);
        }
        AlphaMap post1 = decodeAlphaMap(chunks[1], 1, false);
        CHECK(std::memcmp(post1.texels.data(), a1[1].texels.data(),
                          post1.texels.size()) == 0);

        // Absent fromPath: nothing retargeted, MTEX untouched.
        CHECK(swapTexture(mtex, chunks, "Tileset\\nowhere.blp",
                          "Tileset\\other.blp", SwapScope::Tile) == 0);
        CHECK(mtex.size() == 4);
        // Swapping a path onto itself (case-insensitively) is a no-op.
        CHECK(swapTexture(mtex, chunks, "Tileset\\Elwynn\\dirt.blp",
                          "TILESET\\ELWYNN\\DIRT.BLP", SwapScope::Tile) == 0);
        CHECK(chunks[0].layers[1].textureId == 1);
    }

    // --- swapTexture(Chunk): duplicate collapse merges by max ----------------
    {
        std::vector<std::string> mtex = {
            "Tileset\\base.blp", "Tileset\\dirt.blp", "Tileset\\rock.blp",
        };
        std::vector<MapChunk> chunks = { makeChunk({0, 1, 2}), makeChunk({0, 1}) };
        std::vector<AlphaMap> a0(3);
        a0[1].texels[0] = 170; a0[1].texels[1] = 0;    // dirt
        a0[2].texels[0] = 85;  a0[2].texels[1] = 51;   // rock
        commitAlphas(chunks[0], a0, false);

        // dirt -> rock in chunk 0 only: layers 1 and 2 now both name rock.
        int n = swapTexture(mtex, chunks, "Tileset\\dirt.blp",
                            "Tileset\\rock.blp", SwapScope::Chunk, 0);
        CHECK(n == 1);
        CHECK(chunks[0].layers.size() == 2);           // duplicate collapsed
        CHECK(chunks[0].layers[1].textureId == 2);
        AlphaMap merged = decodeAlphaMap(chunks[0], 1, false);
        CHECK(merged.texels[0] == 170);                // max(170, 85)
        CHECK(merged.texels[1] == 51);                 // max(0, 51)
        CHECK(chunks[1].layers[1].textureId == 1);     // out of scope: untouched
        CHECK(chunks[0].layers[1].ofsAlpha == 0);      // repacked blob consistent
        CHECK(chunks[0].alpha.size() == 2048);         // one 4-bit layer left

        // Brush-scope list variant hits only the listed chunks.
        int m = swapTextureInChunks(mtex, chunks, "Tileset\\dirt.blp",
                                    "Tileset\\rock.blp", {1, 99});
        CHECK(m == 1);
        CHECK(chunks[1].layers[1].textureId == 2);
    }

    // --- commitAlphas -> decodeAlphaMap round-trips ---------------------------
    {
        MapChunk mc = makeChunk({0, 1, 2});
        std::vector<AlphaMap> maps(3);
        for (size_t k = 0; k < maps[1].texels.size(); ++k) {
            maps[1].texels[k] = static_cast<uint8_t>((k * 3) & 0xFF);
            maps[2].texels[k] = static_cast<uint8_t>((k * 7 + 13) & 0xFF);
        }

        // 4-bit vanilla path: quantised to multiples of 17, max delta <= 17.
        commitAlphas(mc, maps, false);
        CHECK(mc.alpha.size() == 2 * 2048);
        CHECK((mc.layers[0].flags & MCLY_USE_ALPHA) == 0);
        CHECK((mc.layers[1].flags & MCLY_USE_ALPHA) != 0);
        CHECK((mc.layers[1].flags & MCLY_COMPRESSED) == 0);
        CHECK(mc.layers[1].ofsAlpha == 0);
        CHECK(mc.layers[2].ofsAlpha == 2048);
        int maxDelta = 0;
        for (size_t li = 1; li <= 2; ++li) {
            AlphaMap dec = decodeAlphaMap(mc, li, false);
            for (size_t k = 0; k < dec.texels.size(); ++k) {
                int d = std::abs(int(dec.texels[k]) - int(maps[li].texels[k]));
                if (d > maxDelta) maxDelta = d;
            }
        }
        CHECK(maxDelta <= 17);

        // 8-bit (bigAlpha) path: loss-less.
        commitAlphas(mc, maps, true);
        CHECK(mc.alpha.size() == 2 * 4096);
        CHECK(mc.layers[2].ofsAlpha == 4096);
        AlphaMap dec = decodeAlphaMap(mc, 1, true);
        CHECK(std::memcmp(dec.texels.data(), maps[1].texels.data(),
                          dec.texels.size()) == 0);
    }
}
