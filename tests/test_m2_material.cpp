#include "test.hpp"
#include "anim.hpp"
#include "m2.hpp"
#include "m2_render.hpp"

#include <cstring>
#include <vector>

// Material-level M2 animation: color (RGB) + alpha and texture-weight
// (transparency) tracks. These ride the same 28-byte AnimationBlock / M2Track
// machinery as bones (RawChannel<T> + readChannel<T>), decoded here as typed
// channels: RGB = Vec3, alpha/weight = fixed16 (int16 / 32767). The tests build
// a synthetic vanilla M2 (version 0x100) with known tracks -- including a
// global-sequence-driven alpha -- then verify both the parse and the sampling
// (sampleM2Color / sampleM2TextureWeight / sampleM2Tint, plus the renderer's
// submeshTint), so a posed model exposes an animated per-submesh tint.

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void putf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(b,v); }
void puts16(std::vector<uint8_t>& b, int16_t v){ put16(b, (uint16_t)v); }
void patch32(std::vector<uint8_t>& b, size_t at, uint32_t v){ for(int i=0;i<4;i++) b[at+i]=(v>>(8*i))&0xFF; }

// Append a 28-byte AnimationBlock and return its offset. The range/time/value
// arrays must already be laid down in `f`; we just point at them.
uint32_t putAnimBlock(std::vector<uint8_t>& f, uint16_t interp, int16_t globalSeq,
                      uint32_t nRanges, uint32_t ofsRanges,
                      uint32_t nTimes,  uint32_t ofsTimes,
                      uint32_t nKeys,   uint32_t ofsKeys) {
    uint32_t at = (uint32_t)f.size();
    put16(f, interp);
    put16(f, (uint16_t)globalSeq);
    put32(f, nRanges); put32(f, ofsRanges);
    put32(f, nTimes);  put32(f, ofsTimes);
    put32(f, nKeys);   put32(f, ofsKeys);
    return at;
}
} // namespace

void test_m2_material() {
    std::printf("[m2.material]\n");

    // ===================================================================
    // Build a synthetic vanilla M2 (version 0x100) with material tracks.
    // ===================================================================
    std::vector<uint8_t> f(0x150, 0);
    f[0]='M'; f[1]='D'; f[2]='2'; f[3]='0';
    patch32(f, 0x004, 0x100);

    // ---- shared keyframe arrays (laid down first, blocks point at them) ----

    // Color RGB track: 2 keys over [0,1000] ms, (1,0,0) -> (0,0,1).
    uint32_t rgbRangesOff = (uint32_t)f.size(); put32(f,0); put32(f,1);     // range [0,1]
    uint32_t rgbTimesOff  = (uint32_t)f.size(); put32(f,0); put32(f,1000);
    uint32_t rgbValsOff   = (uint32_t)f.size();
    putf(f,1);putf(f,0);putf(f,0);            // (1,0,0) red
    putf(f,0);putf(f,0);putf(f,1);            // (0,0,1) blue

    // Color alpha track (fixed16): 2 keys over [0,1000], 0.0 -> 1.0.
    uint32_t aRangesOff = (uint32_t)f.size(); put32(f,0); put32(f,1);
    uint32_t aTimesOff  = (uint32_t)f.size(); put32(f,0); put32(f,1000);
    uint32_t aValsOff   = (uint32_t)f.size();
    puts16(f, 0);                              // 0/32767 = 0.0
    puts16(f, 32767);                          // 32767/32767 = 1.0

    // Texture-weight track (fixed16): GLOBAL SEQUENCE 0 (duration 2000 ms).
    // 2 keys over [0,2000], 1.0 -> 0.0 (a torch fading out on the global clock).
    uint32_t wRangesOff = (uint32_t)f.size(); put32(f,0); put32(f,0);   // single range covering whole track
    uint32_t wTimesOff  = (uint32_t)f.size(); put32(f,0); put32(f,2000);
    uint32_t wValsOff    = (uint32_t)f.size();
    puts16(f, 32767);                          // 1.0
    puts16(f, 0);                              // 0.0

    // ---- ModelColorDef[1]: AnimationBlock rgb (Vec3) + AnimationBlock alpha (fixed16) ----
    uint32_t colorsOff = (uint32_t)f.size();
    putAnimBlock(f, /*interp*/1, /*globalSeq*/-1, 1, rgbRangesOff, 2, rgbTimesOff, 2, rgbValsOff);
    putAnimBlock(f, /*interp*/1, /*globalSeq*/-1, 1, aRangesOff,   2, aTimesOff,   2, aValsOff);

    // ---- ModelTransDef[1]: one AnimationBlock weight (fixed16) on global seq 0 ----
    uint32_t transOff = (uint32_t)f.size();
    putAnimBlock(f, /*interp*/1, /*globalSeq*/0, 1, wRangesOff, 2, wTimesOff, 2, wValsOff);

    // ---- global sequences: one entry, duration 2000 ms ----
    uint32_t globalSeqOff = (uint32_t)f.size();
    put32(f, 2000);

    // ---- transparency lookup table: [0] -> textureWeights[0] (identity) ----
    uint32_t transLookupOff = (uint32_t)f.size();
    put16(f, 0);

    // ---- patch header arrays ----
    patch32(f, 0x010, 1); patch32(f, 0x014, globalSeqOff);   // nGlobalSequences
    patch32(f, 0x044, 1); patch32(f, 0x048, colorsOff);      // nColors
    patch32(f, 0x054, 1); patch32(f, 0x058, transOff);       // nTransparency
    patch32(f, 0x08C, 1); patch32(f, 0x090, transLookupOff); // nTransparencyLookup

    // ===================================================================
    // Parse.
    // ===================================================================
    M2Animation anim = parseM2Animation(f);

    CHECK(anim.globalSeqs.size() == 1);
    CHECK(anim.globalSeqs[0] == 2000);
    CHECK(anim.colors.size() == 1);
    CHECK(anim.textureWeights.size() == 1);
    CHECK(anim.transparencyLookup.size() == 1);
    CHECK(anim.transparencyLookup[0] == 0);

    // Strides / decode: RGB track is Vec3, alpha track is fixed16-decoded float.
    CHECK(anim.colors[0].rgb.times.size() == 2);
    CHECK(anim.colors[0].rgb.values.size() == 2);
    CHECK_APPROX(anim.colors[0].rgb.values[0].x, 1.0f);   // red
    CHECK_APPROX(anim.colors[0].rgb.values[1].z, 1.0f);   // blue
    CHECK(anim.colors[0].alpha.values.size() == 2);
    CHECK_APPROX(anim.colors[0].alpha.values[0], 0.0f);
    CHECK_APPROX(anim.colors[0].alpha.values[1], 1.0f);   // 32767/32767
    CHECK(anim.textureWeights[0].weight.globalSeq == 0);  // driven by global seq 0
    CHECK_APPROX(anim.textureWeights[0].weight.values[0], 1.0f);
    CHECK_APPROX(anim.textureWeights[0].weight.values[1], 0.0f);

    // ===================================================================
    // Sample: color RGB + alpha at the animation midpoint (t=500 / 1000).
    // ===================================================================
    {
        M2Tint c = sampleM2Color(anim, /*colorIndex*/0, /*animIndex*/0,
                                 /*animTimeMs*/500, /*globalTimeMs*/0);
        CHECK_APPROX(c.rgb.x, 0.5f);   // lerp red->blue
        CHECK_APPROX(c.rgb.y, 0.0f);
        CHECK_APPROX(c.rgb.z, 0.5f);
        CHECK_APPROX(c.alpha, 0.5f);   // lerp 0->1
    }
    // Endpoints clamp.
    CHECK_APPROX(sampleM2Color(anim, 0, 0, 0,    0).alpha, 0.0f);
    CHECK_APPROX(sampleM2Color(anim, 0, 0, 1000, 0).alpha, 1.0f);
    // Out-of-range color index -> identity tint.
    {
        M2Tint id = sampleM2Color(anim, /*colorIndex*/-1, 0, 500, 0);
        CHECK_APPROX(id.rgb.x, 1.0f); CHECK_APPROX(id.alpha, 1.0f);
    }

    // ===================================================================
    // Sample: texture-weight on a GLOBAL SEQUENCE (ignores animTimeMs;
    // uses globalTimeMs % 2000). Weight ramps 1.0 -> 0.0 across [0,2000].
    // ===================================================================
    {
        // globalTimeMs = 1000 -> midpoint of the global clock -> weight 0.5,
        // regardless of animTimeMs.
        float w = sampleM2TextureWeight(anim, /*weightIndex*/0, /*animIndex*/0,
                                        /*animTimeMs*/0, /*globalTimeMs*/1000);
        CHECK_APPROX(w, 0.5f);
        // Looping: 3000 % 2000 = 1000 -> same 0.5.
        CHECK_APPROX(sampleM2TextureWeight(anim, 0, 0, 0, 3000), 0.5f);
        CHECK_APPROX(sampleM2TextureWeight(anim, 0, 0, 0, 0),    1.0f);
        // The global clock loops: 2000 % 2000 == 0, so the duration wraps back
        // to the start of the ramp (1.0), it does NOT clamp at the end.
        CHECK_APPROX(sampleM2TextureWeight(anim, 0, 0, 0, 2000), 1.0f);
        // Sampling just before the wrap is near the end of the ramp (~0.0).
        CHECK_NEAR(sampleM2TextureWeight(anim, 0, 0, 0, 1999), 0.0f, 1e-3);
        // Out-of-range -> 1.0 (fully opaque).
        CHECK_APPROX(sampleM2TextureWeight(anim, -1, 0, 0, 0),   1.0f);
    }

    // ===================================================================
    // Combined tint: alpha = colorAlpha * textureWeight.
    // At animTime=500 (colorAlpha 0.5) and globalTime=1000 (weight 0.5),
    // combined alpha = 0.25; RGB still the color track's 0.5/0/0.5.
    // ===================================================================
    {
        M2Tint t = sampleM2Tint(anim, /*colorIndex*/0, /*weightIndex*/0,
                                /*animIndex*/0, /*animTimeMs*/500, /*globalTimeMs*/1000);
        CHECK_APPROX(t.rgb.x, 0.5f);
        CHECK_APPROX(t.rgb.z, 0.5f);
        CHECK_APPROX(t.alpha, 0.25f);   // 0.5 colorAlpha * 0.5 weight
    }

    // The renderer-facing helper is the same evaluation (per-submesh tint the
    // textured rasteriser modulates the texel by: texelA * colorA * weight).
    {
        M2Tint r = submeshTint(anim, 0, 0, 0, 500, 1000);
        CHECK_APPROX(r.rgb.x, 0.5f);
        CHECK_APPROX(r.alpha, 0.25f);
        // No indices -> identity, texel passes through untouched.
        M2Tint none = submeshTint(anim, -1, -1, 0, 500, 1000);
        CHECK_APPROX(none.rgb.x, 1.0f);
        CHECK_APPROX(none.alpha, 1.0f);
    }

    // ===================================================================
    // A negative fixed16 weight decodes to a negative float (sign preserved),
    // confirming the int16/32767 decode rather than a uint16 read.
    // ===================================================================
    {
        std::vector<uint8_t> g(0x150, 0);
        g[0]='M'; g[1]='D'; g[2]='2'; g[3]='0';
        patch32(g, 0x004, 0x100);
        uint32_t rOff = (uint32_t)g.size(); put32(g,0); put32(g,0);
        uint32_t tOff = (uint32_t)g.size(); put32(g,0);
        uint32_t vOff = (uint32_t)g.size(); puts16(g, -16384);   // -16384/32767 ~= -0.5
        uint32_t tdOff = (uint32_t)g.size();
        putAnimBlock(g, 0, -1, 1, rOff, 1, tOff, 1, vOff);
        patch32(g, 0x054, 1); patch32(g, 0x058, tdOff);
        M2Animation a2 = parseM2Animation(g);
        CHECK(a2.textureWeights.size() == 1);
        CHECK_APPROX(a2.textureWeights[0].weight.values[0], -16384.0f/32767.0f);
        CHECK(a2.textureWeights[0].weight.values[0] < 0.0f);
    }

    // --- blend-mode -> raster state (T2.2) ----------------------------------
    {
        M2RasterState op = resolveM2Material(M2BlendMode::Opaque, 0);
        CHECK(!op.alphaTest && !op.alphaBlend && !op.emissive && op.writeDepth);

        M2RasterState ak = resolveM2Material(M2BlendMode::AlphaKey, 0);
        CHECK(ak.alphaTest && !ak.alphaBlend && ak.writeDepth);      // cutout writes depth

        M2RasterState al = resolveM2Material(M2BlendMode::Alpha, 0);
        CHECK(al.alphaBlend && !al.writeDepth && !al.emissive);      // translucent, no z-write

        for (M2BlendMode add : { M2BlendMode::Add, M2BlendMode::BlendAdd }) {
            M2RasterState a = resolveM2Material(add, 0);
            CHECK(a.alphaBlend && a.emissive && a.unlit && !a.writeDepth);
        }
        for (M2BlendMode mod : { M2BlendMode::Mod, M2BlendMode::Mod2x }) {
            M2RasterState m = resolveM2Material(mod, 0);
            CHECK(m.alphaBlend && !m.emissive && !m.writeDepth);
        }

        // Flag bits: unlit / two-sided / forced no-z-write on an opaque material.
        M2RasterState f = resolveM2Material(M2BlendMode::Opaque,
                                            M2RF_UNLIT | M2RF_TWO_SIDED | M2RF_NO_ZWRITE);
        CHECK(f.unlit && f.twoSided && !f.writeDepth);
        // Unknown blend value falls back to opaque, not garbage.
        M2RasterState u = resolveM2Material(static_cast<M2BlendMode>(99), 0);
        CHECK(!u.alphaBlend && !u.alphaTest && u.writeDepth);
    }

    // --- geoset id decode + selection (T2.2) --------------------------------
    {
        CHECK(decodeGeosetId(0).base);
        M2GeosetId g1 = decodeGeosetId(101);   // group 1, variation 1
        CHECK(!g1.base && g1.group == 1 && g1.variation == 1);
        M2GeosetId g2 = decodeGeosetId(702);   // group 7, variation 2
        CHECK(g2.group == 7 && g2.variation == 2);

        std::vector<M2Submesh> subs;
        auto mk = [](uint16_t id) { M2Submesh s; s.id = id; return s; };
        subs.push_back(mk(0));      // 0: base skin      (idx 0)
        subs.push_back(mk(101));    // 1: group1 var1    (idx 1)
        subs.push_back(mk(102));    // 2: group1 var2    (idx 2)
        subs.push_back(mk(201));    // 3: group2 var1    (idx 3)

        // Default: base + lowest variation of each group -> {0,101,201}.
        std::vector<uint32_t> def = selectGeosets(subs, {});
        CHECK((def == std::vector<uint32_t>{0, 1, 3}));

        // Choose group1 variation 2 -> {0,102,201}; group2 unspecified -> its lowest.
        std::vector<uint32_t> pick = selectGeosets(subs, { {1, 2} });
        CHECK((pick == std::vector<uint32_t>{0, 2, 3}));

        // Choosing a variation that doesn't exist drops that group entirely
        // (only base + other groups' defaults remain).
        std::vector<uint32_t> miss = selectGeosets(subs, { {1, 9} });
        CHECK((miss == std::vector<uint32_t>{0, 3}));
    }

    // --- skinM2Geosets filters triangles by selected submesh ----------------
    {
        // Two submeshes over 6 vertices / 2 triangles: submesh id 0 (tri 0),
        // submesh id 101 (tri 1). Selecting the base only must drop tri 1.
        M2Model m;
        m.version = 0x100;
        for (int i = 0; i < 6; ++i)
            m.vertices.push_back(M2Vertex{ Vec3{float(i),0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,1}, {0,0} });
        m.vertexLookup = { 0,1,2,3,4,5 };
        m.triangles    = { 0,1,2, 3,4,5 };
        M2Submesh s0; s0.id = 0;   s0.indexStart = 0; s0.indexCount = 3; s0.vertexStart=0; s0.vertexCount=3;
        M2Submesh s1; s1.id = 101; s1.indexStart = 3; s1.indexCount = 3; s1.vertexStart=3; s1.vertexCount=3;
        m.submeshes = { s0, s1 };

        TexMesh all = skinM2(m, {});
        CHECK(all.indices.size() == 6);                       // both triangles

        // Choosing group1 variation 2 (absent) leaves only the base geoset.
        TexMesh baseOnly = skinM2Geosets(m, {}, { {1, 2} });
        CHECK(baseOnly.indices.size() == 3);                  // just triangle 0
        CHECK(baseOnly.indices[0] == 0 && baseOnly.indices[2] == 2);

        // Default selection draws base + group1's only variation -> both tris.
        TexMesh def = skinM2Geosets(m, {}, {});
        CHECK(def.indices.size() == 6);
    }
}
