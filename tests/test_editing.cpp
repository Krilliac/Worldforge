#include "test.hpp"
#include "editing.hpp"
#include "gizmo.hpp"
#include "coords.hpp"

#include <vector>

using namespace wf;

// A flat chunk at tile-local grid (ix,iy) with all heights 0 and base z 0.
static MapChunk flatChunk(uint32_t ix, uint32_t iy) {
    MapChunk mc;
    mc.indexX   = ix;        // west-east column
    mc.indexY   = iy;        // north-south row
    mc.position = {0, 0, 0};
    mc.heights.fill(0.0f);
    return mc;
}

void test_editing() {
    std::printf("[editing]\n");

    // --- falloff weights -----------------------------------------------------
    CHECK_APPROX(falloffWeight(Falloff::Linear, 0.0f, 10.0f), 1.0f);  // centre
    CHECK_APPROX(falloffWeight(Falloff::Linear, 10.0f, 10.0f), 0.0f); // edge
    CHECK_APPROX(falloffWeight(Falloff::Linear, 5.0f, 10.0f), 0.5f);  // midpoint
    CHECK(falloffWeight(Falloff::Smooth, 5.0f, 10.0f) > 0.0f);
    CHECK(falloffWeight(Falloff::Flat, 9.9f, 10.0f) == 1.0f);         // flat = full
    CHECK(falloffWeight(Falloff::Linear, 11.0f, 10.0f) == 0.0f);      // outside
    // innerRatio holds full strength inside the inner band.
    CHECK_APPROX(falloffWeight(Falloff::Linear, 4.0f, 10.0f, 0.5f), 1.0f);
    CHECK(falloffWeight(Falloff::Linear, 6.0f, 10.0f, 0.5f) < 1.0f);

    // --- raise / lower terrain ----------------------------------------------
    std::vector<Vertex> verts = {
        {{0, 0, 0}, {0, 0, 1}},      // under brush centre
        {{5, 0, 0}, {0, 0, 1}},      // partway out
        {{100, 0, 0}, {0, 0, 1}},    // far outside the radius
    };
    Brush b; b.center = {0, 0, 0}; b.radius = 10.0f; b.strength = 4.0f;
    b.falloff = Falloff::Linear;
    int hits = brushRaiseLower(verts, b, +1.0f);
    CHECK(hits == 2);                                  // the far vertex untouched
    CHECK_APPROX(verts[0].position.z, 4.0f);          // centre: full strength
    CHECK_APPROX(verts[1].position.z, 2.0f);          // half falloff -> 2.0
    CHECK_APPROX(verts[2].position.z, 0.0f);          // outside -> unchanged

    // Lower undoes the raise on the centre vertex.
    brushRaiseLower(verts, b, -1.0f);
    CHECK_APPROX(verts[0].position.z, 0.0f);

    // --- flatten toward a target height -------------------------------------
    std::vector<Vertex> hill = { {{0, 0, 10.0f}, {0,0,1}} };
    Brush fb; fb.center = {0,0,0}; fb.radius = 10.0f; fb.strength = 0.5f;
    fb.falloff = Falloff::Flat;
    brushFlatten(hill, fb, 0.0f);
    CHECK_APPROX(hill[0].position.z, 5.0f);           // halfway to target
    brushFlatten(hill, fb, 0.0f);
    CHECK_APPROX(hill[0].position.z, 2.5f);           // halfway again

    // --- raise/lower directly on the source MCNK height grids ---------------
    // Tile 32,32 chunk Index(0,0) places its outer corner at world (0,0), so a
    // brush there edits MCVT[0] (the chunk's NW-most height sample) in place.
    {
        std::vector<MapChunk> chunks = { flatChunk(0, 0) };
        Brush cb; cb.center = {0, 0, 0}; cb.radius = 2.0f; cb.strength = 5.0f;
        cb.falloff = Falloff::Flat;
        int h = brushRaiseLowerChunks(chunks, 32, 32, cb, +1.0f);
        CHECK(h == 1);                                  // only the corner sample
        CHECK_APPROX(chunks[0].heights[0], 5.0f);       // MCVT[0] raised
        CHECK_APPROX(chunks[0].heights[1], 0.0f);       // neighbour out of radius
        // Lower undoes the raise.
        brushRaiseLowerChunks(chunks, 32, 32, cb, -1.0f);
        CHECK_APPROX(chunks[0].heights[0], 0.0f);
    }

    // --- flatten source heights toward a world target Z ---------------------
    {
        std::vector<MapChunk> chunks = { flatChunk(0, 0) };
        chunks[0].heights[0] = 10.0f;                   // a spike at the corner
        Brush fbc; fbc.center = {0, 0, 0}; fbc.radius = 2.0f; fbc.strength = 0.5f;
        fbc.falloff = Falloff::Flat;
        brushFlattenChunks(chunks, 32, 32, fbc, 0.0f);
        CHECK_APPROX(chunks[0].heights[0], 5.0f);       // halfway to world Z 0
    }

    // --- shared chunk edge stays seamless -----------------------------------
    // Chunk0 col-8 outer vertices coincide with chunk1 col-0 ones; a brush on
    // that edge must move both chunks' MCVT identically (no crack at the border).
    {
        std::vector<MapChunk> chunks = { flatChunk(0, 0), flatChunk(1, 0) };
        Brush eb; eb.center = {0.0f, -static_cast<float>(CHUNK_SIZE), 0.0f};
        eb.radius = 1.0f; eb.strength = 3.0f; eb.falloff = Falloff::Flat;
        brushRaiseLowerChunks(chunks, 32, 32, eb, +1.0f);
        CHECK_APPROX(chunks[0].heights[8], 3.0f);       // chunk0 outer(0,8)
        CHECK_APPROX(chunks[1].heights[0], 3.0f);       // chunk1 outer(0,0)
    }

    // --- paint alpha coverage -----------------------------------------------
    AlphaMap am;                                       // starts all-zero
    int painted = paintAlpha(am, 0.5f, 0.5f, 0.25f, 1.0f, Falloff::Flat, 255);
    CHECK(painted > 0);
    CHECK(am.at(32, 32) == 255);                       // centre fully painted
    CHECK(am.at(0, 0) == 0);                           // corner outside the brush
    // Half strength only moves halfway toward the target on a fresh map.
    AlphaMap am2;
    paintAlpha(am2, 0.5f, 0.5f, 0.25f, 0.5f, Falloff::Flat, 200);
    CHECK(am2.at(32, 32) == 100);

    // --- expanded falloff family ---------------------------------------------
    {
        // Serialized ints must stay stable: new profiles are appended only.
        CHECK(static_cast<int>(Falloff::Gaussian)      == 3);
        CHECK(static_cast<int>(Falloff::Polynomial)    == 4);
        CHECK(static_cast<int>(Falloff::Trigonometric) == 5);
        CHECK(static_cast<int>(Falloff::Quadratic)     == 6);

        const float R = 10.0f, IR = 0.2f;               // inner edge at dist 2
        const Falloff all[] = { Falloff::Flat, Falloff::Linear, Falloff::Smooth,
                                Falloff::Gaussian, Falloff::Polynomial,
                                Falloff::Trigonometric, Falloff::Quadratic };
        for (Falloff f : all) {
            CHECK_APPROX(falloffWeight(f, 0.0f, R, IR), 1.0f);     // centre
            CHECK_APPROX(falloffWeight(f, IR * R, R, IR), 1.0f);   // inner edge
            CHECK(falloffWeight(f, R, R, IR) == 0.0f);             // at radius
            CHECK(falloffWeight(f, R + 1.0f, R, IR) == 0.0f);      // beyond
            CHECK(falloffWeight(f, 5.0f, 0.0f, IR) == 0.0f);       // radius <= 0
        }
        // Band midpoint (dist 6 -> t = 0.5), hand-computed expected weights.
        CHECK_APPROX(falloffWeight(Falloff::Linear,        6.0f, R, IR), 0.5f);
        CHECK_APPROX(falloffWeight(Falloff::Smooth,        6.0f, R, IR), 0.5f);
        CHECK_APPROX(falloffWeight(Falloff::Polynomial,    6.0f, R, IR), 0.875f);     // 1 - 0.5^3
        CHECK_APPROX(falloffWeight(Falloff::Trigonometric, 6.0f, R, IR), 0.7071068f); // cos(pi/4)
        CHECK_APPROX(falloffWeight(Falloff::Quadratic,     6.0f, R, IR), 0.75f);      // 1 - 0.5^2
        // Just inside the radius every shaped curve has all but faded out.
        CHECK_NEAR(falloffWeight(Falloff::Polynomial,    R - 1e-3f, R, IR), 0.0f, 1e-3);
        CHECK_NEAR(falloffWeight(Falloff::Trigonometric, R - 1e-3f, R, IR), 0.0f, 1e-3);
        CHECK_NEAR(falloffWeight(Falloff::Quadratic,     R - 1e-3f, R, IR), 0.0f, 1e-3);
    }

    // --- flatten toward an angled plane (30-degree ramp) ---------------------
    // Orientation 90 slopes along +Y (west); the chunk corner sits at world
    // (0,0) so every sample's target is tan(30deg) * its west coordinate.
    {
        std::vector<MapChunk> chunks = { flatChunk(0, 0) };
        FlattenPlane plane;
        plane.lock = {0, 0, 0};
        plane.orientationDeg = 90.0f;
        plane.angleDeg = 30.0f;
        Brush pb; pb.center = { -16.0f, -16.0f, 0 };
        pb.radius = 100.0f; pb.strength = 1.0f; pb.falloff = Falloff::Flat;
        brushFlattenChunks(chunks, 32, 32, pb, plane, FlattenMode::Both);
        const float slope = static_cast<float>(std::tan(radians(30.0)));
        for (int i = 0; i < 9; ++i)
            for (int j = 0; j < 9; ++j) {
                const float wy = -j * static_cast<float>(UNIT_SIZE);
                CHECK_NEAR(chunks[0].heights[i * 17 + j], slope * wy, 1e-4);
            }
        // Full strength lands exactly on the plane and never overshoots it:
        // a second pass changes nothing.
        auto once = chunks[0].heights;
        brushFlattenChunks(chunks, 32, 32, pb, plane, FlattenMode::Both);
        CHECK(chunks[0].heights == once);

        // The plane helper wraps orientation and clamps the angle.
        CHECK_APPROX(flattenPlaneTarget(plane, 0.0f, -10.0f), slope * -10.0f);
        FlattenPlane wrap = plane; wrap.orientationDeg = 450.0f;   // wraps to 90
        CHECK_APPROX(flattenPlaneTarget(wrap, 0.0f, -10.0f), slope * -10.0f);
        FlattenPlane steep = plane; steep.angleDeg = 200.0f;       // clamps to 89
        CHECK_APPROX(flattenPlaneTarget(steep, 0.0f, 1.0f),
                     static_cast<float>(std::tan(radians(89.0))));
    }

    // --- flatten modes: RaiseOnly never lowers, LowerOnly never raises -------
    {
        FlattenPlane level; level.lock = {0, 0, 0};    // horizontal plane at z 0
        Brush pb; pb.center = { -16.0f, -16.0f, 0 };
        pb.radius = 100.0f; pb.strength = 0.5f; pb.falloff = Falloff::Flat;

        std::vector<MapChunk> up = { flatChunk(0, 0) };
        for (int m = 0; m < 145; ++m) up[0].heights[m] = (m % 2 == 0) ? 5.0f : -5.0f;
        auto before = up[0].heights;
        brushFlattenChunks(up, 32, 32, pb, level, FlattenMode::RaiseOnly);
        for (int m = 0; m < 145; ++m) CHECK(up[0].heights[m] >= before[m]);
        CHECK_APPROX(up[0].heights[0], 5.0f);          // above target: untouched
        CHECK_APPROX(up[0].heights[1], -2.5f);         // below target: halfway up

        std::vector<MapChunk> down = { flatChunk(0, 0) };
        down[0].heights = before;
        brushFlattenChunks(down, 32, 32, pb, level, FlattenMode::LowerOnly);
        for (int m = 0; m < 145; ++m) CHECK(down[0].heights[m] <= before[m]);
        CHECK_APPROX(down[0].heights[0], 2.5f);        // above target: halfway down
        CHECK_APPROX(down[0].heights[1], -5.0f);       // below target: untouched

        std::vector<MapChunk> both = { flatChunk(0, 0) };
        both[0].heights = before;
        brushFlattenChunks(both, 32, 32, pb, level, FlattenMode::Both);
        CHECK_APPROX(both[0].heights[0], 2.5f);        // converges from both sides
        CHECK_APPROX(both[0].heights[1], -2.5f);

        // Mesh-vertex variant of the angled flatten (tan 45 = 1).
        std::vector<Vertex> ramp = { {{0, -10, 0}, {0,0,1}}, {{0, 0, 7}, {0,0,1}} };
        FlattenPlane rp; rp.lock = {0,0,0}; rp.orientationDeg = 90.0f; rp.angleDeg = 45.0f;
        Brush rb; rb.center = {0,0,0}; rb.radius = 50.0f; rb.strength = 1.0f;
        rb.falloff = Falloff::Flat;
        brushFlatten(ramp, rb, rp, FlattenMode::Both);
        CHECK_APPROX(ramp[0].position.z, -10.0f);
        CHECK_APPROX(ramp[1].position.z, 0.0f);        // sits on the lock point
    }

    // --- blur/smooth: spike diffuses, chunk seams stay bit-identical ---------
    {
        std::vector<MapChunk> chunks = { flatChunk(0, 0), flatChunk(1, 0) };
        // A spike on the shared border: chunk0 outer(4,8) == chunk1 outer(4,0).
        chunks[0].heights[4 * 17 + 8] = 10.0f;
        chunks[1].heights[4 * 17 + 0] = 10.0f;
        Brush sb;
        sb.center = { -4.0f * static_cast<float>(UNIT_SIZE),
                      -static_cast<float>(CHUNK_SIZE), 0 };
        sb.radius = 12.0f; sb.strength = 0.5f; sb.falloff = Falloff::Smooth;
        auto variance = [&]() {
            double sum = 0, sum2 = 0; int n = 0;
            for (const MapChunk& mc : chunks)
                for (float h : mc.heights) { sum += h; sum2 += h * h; ++n; }
            const double mean = sum / n;
            return sum2 / n - mean * mean;
        };
        double prev = variance();
        for (int pass = 0; pass < 5; ++pass) {
            CHECK(brushSmoothChunks(chunks, 32, 32, sb) > 0);
            const double var = variance();
            CHECK(var < prev);                          // strictly smoother each pass
            prev = var;
            for (int i = 0; i < 9; ++i)                 // duplicated border samples
                CHECK(chunks[0].heights[i * 17 + 8] == chunks[1].heights[i * 17 + 0]);
        }
        CHECK(chunks[0].heights[4 * 17 + 8] < 10.0f);   // the spike came down
        CHECK(chunks[0].heights[4 * 17 + 8] > 0.0f);    // but not past the floor
        // Strength 0 is a no-op.
        auto snap = chunks[0].heights;
        Brush zb = sb; zb.strength = 0.0f;
        CHECK(brushSmoothChunks(chunks, 32, 32, zb) == 0);
        CHECK(chunks[0].heights == snap);
    }

    // --- paintAlpha hardness/pressure ----------------------------------------
    {
        AlphaMap hard;   // hardness 1: the whole radius paints at full strength
        paintAlpha(hard, 0.5f, 0.5f, 0.25f, 1.0f, 1.0f, Falloff::Linear, 255);
        CHECK(hard.at(32, 32) == 255);
        CHECK(hard.at(32, 46) == 255);                  // near the rim, still full
        CHECK(hard.at(0, 0) == 0);                      // outside the radius
        AlphaMap soft;   // hardness 0: Linear fades toward the rim
        paintAlpha(soft, 0.5f, 0.5f, 0.25f, 0.0f, 1.0f, Falloff::Linear, 255);
        // Texel (32,32)'s centre sits sqrt(0.5) texels off the brush centre
        // (texel-centre convention), so Linear gives w = 1 - 0.7071/16 and
        // lround(255 * 0.95581) == 244 -- not a full 255.
        CHECK(soft.at(32, 32) == 244);
        CHECK(soft.at(32, 46) < 255);
        CHECK(soft.at(32, 46) > 0);
        // Pressure converges on the target and never overshoots it.
        AlphaMap conv;
        paintAlpha(conv, 0.5f, 0.5f, 0.25f, 1.0f, 0.5f, Falloff::Flat, 200);
        CHECK(conv.at(32, 32) == 100);                  // halfway there
        paintAlpha(conv, 0.5f, 0.5f, 0.25f, 1.0f, 0.5f, Falloff::Flat, 200);
        CHECK(conv.at(32, 32) == 150);                  // halfway again
        paintAlpha(conv, 0.5f, 0.5f, 0.25f, 1.0f, 1.0f, Falloff::Flat, 200);
        paintAlpha(conv, 0.5f, 0.5f, 0.25f, 1.0f, 1.0f, Falloff::Flat, 200);
        CHECK(conv.at(32, 32) == 200);                  // pinned at the target
    }

    // --- spray: seeded scatter is exactly reproducible ------------------------
    {
        AlphaMap sprayA, sprayB;
        uint32_t s1 = 12345u, s2 = 12345u;
        int n1 = sprayAlpha(sprayA, 0.5f, 0.5f, 0.3f, 0.05f, 16, s1,
                            1.0f, Falloff::Flat, 255);
        int n2 = sprayAlpha(sprayB, 0.5f, 0.5f, 0.3f, 0.05f, 16, s2,
                            1.0f, Falloff::Flat, 255);
        CHECK(n1 > 0);
        CHECK(n1 == n2);
        CHECK(sprayA.texels == sprayB.texels);          // same seed: byte-identical
        CHECK(s1 == s2);                                // same advanced rng state
        CHECK(s1 != 12345u);                            // and it did advance
        AlphaMap sprayC;
        uint32_t s3 = 999u;
        sprayAlpha(sprayC, 0.5f, 0.5f, 0.3f, 0.05f, 16, s3,
                   1.0f, Falloff::Flat, 255);
        CHECK(!(sprayC.texels == sprayA.texels));       // different seed differs
        // Degenerate parameters paint nothing.
        uint32_t s4 = 7u;
        CHECK(sprayAlpha(sprayC, 0.5f, 0.5f, 0.0f, 0.05f, 16, s4,
                         1.0f, Falloff::Flat, 255) == 0);
        CHECK(sprayAlpha(sprayC, 0.5f, 0.5f, 0.3f, 0.05f, 0, s4,
                         1.0f, Falloff::Flat, 255) == 0);
    }

    // --- image-stamp brush -----------------------------------------------------
    {
        // 4x4 grayscale kernel; the centre four texels average to 100.
        const uint8_t px[16] = {
            255,  10,  20,  30,
             40, 100, 150, 200,
             60,  50, 100,  90,
             70,  80,  90, 120,
        };
        ImageBrush ib; ib.pixels = px; ib.width = 4; ib.height = 4;
        const float R = 4.0f * static_cast<float>(UNIT_SIZE);
        // Footprint corners and centre hit exact kernel texels.
        CHECK_APPROX(imageBrushWeight(ib, -R, -R, R), 255.0f / 255.0f); // u=v=0 -> px[0]
        CHECK_APPROX(imageBrushWeight(ib,  R,  R, R), 120.0f / 255.0f); // u=v=1 -> px[15]
        CHECK_APPROX(imageBrushWeight(ib, 0.0f, 0.0f, R), 100.0f / 255.0f); // bilinear centre
        CHECK(imageBrushWeight(ib, R * 1.01f, 0.0f, R) == 0.0f);        // outside footprint
        CHECK(imageBrushWeight(ib, 0.0f, 0.0f, 0.0f) == 0.0f);          // radius <= 0
        // Rotation 90 maps the kernel's (1,0) axis onto (0,1).
        ImageBrush rot = ib; rot.rotationDeg = 90.0f;
        CHECK_APPROX(imageBrushWeight(rot, 0.0f, R, R),
                     imageBrushWeight(ib, R, 0.0f, R));
        CHECK_APPROX(imageBrushWeight(rot, 0.0f, 0.5f * R, R),
                     imageBrushWeight(ib, 0.5f * R, 0.0f, R));
        // A 1x1 kernel degenerates to a flat brush over the footprint.
        const uint8_t solo = 128;
        ImageBrush one; one.pixels = &solo; one.width = 1; one.height = 1;
        CHECK_APPROX(imageBrushWeight(one, 0.3f * R, -0.6f * R, R), 128.0f / 255.0f);

        // Stamp centred on the middle outer sample (4,4): every outer sample
        // (i,j) then sits at offset ((4-i)U, (4-j)U), i.e. known kernel UVs.
        std::vector<MapChunk> chunks = { flatChunk(0, 0) };
        const Vec3 sc = { -4.0f * static_cast<float>(UNIT_SIZE),
                          -4.0f * static_cast<float>(UNIT_SIZE), 0.0f };
        CHECK(brushStampChunks(chunks, 32, 32, sc, R, 2.0f, +1.0f, ib) > 0);
        CHECK_NEAR(chunks[0].heights[0],           2.0f * 120.0f / 255.0f, 1e-4); // u=v=1
        CHECK_NEAR(chunks[0].heights[8 * 17 + 8],  2.0f * 255.0f / 255.0f, 1e-4); // u=v=0
        CHECK_NEAR(chunks[0].heights[4 * 17 + 4],  2.0f * 100.0f / 255.0f, 1e-4); // centre
        // Duplicated border samples get the same stamp delta across chunks.
        std::vector<MapChunk> border = { flatChunk(0, 0), flatChunk(1, 0) };
        const Vec3 ec = { -4.0f * static_cast<float>(UNIT_SIZE),
                          -static_cast<float>(CHUNK_SIZE), 0.0f };
        brushStampChunks(border, 32, 32, ec, R, 1.0f, +1.0f, one);
        for (int i = 0; i < 9; ++i)
            CHECK_APPROX(border[0].heights[i * 17 + 8], border[1].heights[i * 17 + 0]);
    }
}

void test_gizmo() {
    std::printf("[gizmo]\n");

    // --- ray/plane: straight down onto the ground (z=0) ----------------------
    Ray down{ {0, 0, 50}, {0, 0, -1} };
    float t = 0;
    CHECK(rayPlane(down, {0,0,0}, {0,0,1}, t));
    CHECK_APPROX(t, 50.0f);
    Vec3 hit = down.origin + down.dir * t;
    CHECK_APPROX(hit.z, 0.0f);
    // Parallel ray misses.
    Ray flat{ {0,0,50}, {1,0,0} };
    CHECK(!rayPlane(flat, {0,0,0}, {0,0,1}, t));

    // --- ray/triangle --------------------------------------------------------
    Vec3 a{0,0,0}, bb{10,0,0}, c{0,10,0};
    Ray r{ {2, 2, 5}, {0, 0, -1} };
    CHECK(rayTriangle(r, a, bb, c, t));
    CHECK_APPROX(t, 5.0f);
    Ray miss{ {8, 8, 5}, {0, 0, -1} };                 // outside the triangle
    CHECK(!rayTriangle(miss, a, bb, c, t));

    // --- pick a terrain mesh -------------------------------------------------
    Mesh m;
    m.vertices = { {{0,0,0},{0,0,1}}, {{10,0,0},{0,0,1}},
                   {{0,10,0},{0,0,1}}, {{10,10,0},{0,0,1}} };
    m.indices = { 0,1,2, 1,3,2 };
    Ray pr{ {5, 5, 100}, {0, 0, -1} };
    Vec3 p;
    CHECK(pickMesh(pr, m, p));
    CHECK_APPROX(p.z, 0.0f);
    CHECK_APPROX(p.x, 5.0f);
    CHECK_APPROX(p.y, 5.0f);

    // --- camera ray: centre of screen looks straight along forward -----------
    Ray center = cameraRay({0,0,0}, {1,0,0}, {0,-1,0}, {0,0,1}, 60.0, 1.5, 0.0f, 0.0f);
    CHECK_APPROX(center.dir.x, 1.0f);
    CHECK_APPROX(center.dir.y, 0.0f);
    CHECK_APPROX(center.dir.z, 0.0f);

    // --- snapping ------------------------------------------------------------
    CHECK_APPROX(snap(7.3f, 5.0f), 5.0f);
    CHECK_APPROX(snap(8.0f, 5.0f), 10.0f);
    CHECK_APPROX(snap(3.0f, 0.0f), 3.0f);              // step 0 = no-op
    Vec3 sv = snap(Vec3{2.1f, 4.9f, 7.5f}, 1.0f);
    CHECK_APPROX(sv.x, 2.0f); CHECK_APPROX(sv.y, 5.0f); CHECK_APPROX(sv.z, 8.0f);

    // --- drag along an axis: a vertical pointer move slides along +Z ---------
    Ray from{ {0, -10, 0}, {0, 1, 0} };                // looking +Y at the origin
    Ray to  { {0, -10, 4}, {0, 1, 0} };                // same ray lifted +4 in Z
    Vec3 moved = dragAlongAxis({0,0,0}, {0,0,1}, from, to);
    CHECK_APPROX(moved.z, 4.0f);
    CHECK_APPROX(moved.x, 0.0f);
    CHECK_APPROX(moved.y, 0.0f);
}
