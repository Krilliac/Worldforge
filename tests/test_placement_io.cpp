#include "test.hpp"
#include "placement_io.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "coords.hpp"
#include "math.hpp"

using namespace wf;

namespace {

// Build a doodad whose WORLD position is `world` (stores placement coords).
DoodadDef mkDoodad(uint32_t uid, const Vec3& world, const char* name = "World/Tree.m2") {
    DoodadDef d;
    d.uniqueId  = uid;
    d.modelName = name;
    Vec3 s = worldToPlacement(world);
    d.pos[0] = s.x; d.pos[1] = s.y; d.pos[2] = s.z;
    return d;
}

WmoDef mkWmo(uint32_t uid, const Vec3& world, const char* name = "Buildings/Keep.wmo") {
    WmoDef w;
    w.uniqueId  = uid;
    w.modelName = name;
    Vec3 s = worldToPlacement(world);
    w.pos[0] = s.x; w.pos[1] = s.y; w.pos[2] = s.z;
    return w;
}

// World position of a record's stored placement coords.
template <typename Record>
Vec3 recordWorld(const Record& r) {
    return placementToWorld(Vec3{ r.pos[0], r.pos[1], r.pos[2] });
}

// Where the model's local +Z lands under the stored Euler triple, using the
// exact rotation convention the renderer applies (asset_loader):
// R = Rz(rot[1]) * Ry(rot[0]) * Rx(rot[2]).
Vec3 localUp(const float rot[3]) {
    Mat4 r = Mat4::rotateZ(rot[1]) * Mat4::rotateY(rot[0]) * Mat4::rotateX(rot[2]);
    Vec4 up = r * Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    return { up.x, up.y, up.z };
}

bool sameDoodad(const DoodadDef& a, const DoodadDef& b) {
    return a.uniqueId == b.uniqueId && a.mmidIndex == b.mmidIndex
        && a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.pos[2] == b.pos[2]
        && a.rot[0] == b.rot[0] && a.rot[1] == b.rot[1] && a.rot[2] == b.rot[2]
        && a.scale == b.scale && a.flags == b.flags && a.modelName == b.modelName;
}

} // namespace

void test_placement_io() {
    std::printf("[placement_io]\n");

    // --- round-trip ----------------------------------------------------------
    std::vector<ScenePlacement> in;
    in.push_back({"World/Tree.m2",        {12.5f, -3.25f, 100.0f},   45.0f, 1.0f,  false});
    in.push_back({"Buildings/Keep.wmo",   {-200.75f, 64.0f, -8.5f}, 270.0f, 2.5f,  true});
    in.push_back({"World/Rock.m2",        {0.0f, 0.0f, 0.0f},        0.0f,  0.125f, false});

    std::string text = saveScene(in);
    std::vector<ScenePlacement> out = loadScene(text);

    CHECK(out.size() == in.size());
    for (size_t i = 0; i < out.size() && i < in.size(); ++i) {
        CHECK(out[i].model == in[i].model);
        CHECK(out[i].isWmo == in[i].isWmo);
        CHECK_APPROX(out[i].pos.x, in[i].pos.x);
        CHECK_APPROX(out[i].pos.y, in[i].pos.y);
        CHECK_APPROX(out[i].pos.z, in[i].pos.z);
        CHECK_APPROX(out[i].rotZ,  in[i].rotZ);
        CHECK_APPROX(out[i].scale, in[i].scale);
    }

    // Round-trip must be exact for representable floats (not just approx).
    CHECK(out[1].pos.x == in[1].pos.x);
    CHECK(out[1].rotZ  == in[1].rotZ);
    CHECK(out[2].scale == in[2].scale);

    // --- garbage / empty input return empty without crashing -----------------
    CHECK(loadScene("").empty());
    CHECK(loadScene("garbage\nlines").empty());

    // Header alone yields no placements; blank lines are tolerated.
    CHECK(loadScene("WFSCENE 1\n").empty());
    CHECK(loadScene("WFSCENE 1\n\n\n").empty());

    // =========================================================================
    // UidAllocator: empty scan hands out 1; scanTile folds in tile maxima.
    // =========================================================================
    {
        UidAllocator fresh;
        CHECK(fresh.maxSeen() == 0);
        CHECK(fresh.next() == 1);
        CHECK(fresh.next() == 2);
        CHECK(fresh.maxSeen() == 2);
    }
    {
        // Two synthetic "tiles" with colliding ids across doodads AND wmos.
        std::vector<DoodadDef> dd;
        dd.push_back(mkDoodad(10, {0, 0, 0}));
        dd.push_back(mkDoodad(11, {1, 0, 0}));
        dd.push_back(mkDoodad(10, {2, 0, 0}));   // collides with dd[0]
        std::vector<WmoDef> ww;
        ww.push_back(mkWmo(11, {3, 0, 0}));      // collides with dd[1]
        ww.push_back(mkWmo(20, {4, 0, 0}));

        UidAllocator uids;
        uids.scanTile(dd, ww);
        CHECK(uids.maxSeen() == 20);

        int reassigned = fixDuplicateUids(dd, ww, uids);
        CHECK(reassigned == 2);                          // exactly the collisions
        CHECK(dd[0].uniqueId == 10);                     // first occurrences kept
        CHECK(dd[1].uniqueId == 11);
        CHECK(ww[1].uniqueId == 20);
        CHECK(dd[2].uniqueId > 20);                      // fresh, above old max
        CHECK(ww[0].uniqueId > 20);
        CHECK(dd[2].uniqueId != ww[0].uniqueId);

        uint32_t maxAfter = uids.maxSeen();
        CHECK(uids.next() == maxAfter + 1);              // next() > maxSeen()

        // Second pass finds nothing left to fix.
        CHECK(fixDuplicateUids(dd, ww, uids) == 0);
    }

    // =========================================================================
    // Clipboard: median pivot + relative positions.
    // =========================================================================
    {
        std::vector<DoodadDef> dd;
        dd.push_back(mkDoodad(1, { 0.0f, 0.0f, 10.0f}));
        dd.push_back(mkDoodad(2, {10.0f, 4.0f, 20.0f}));
        dd.push_back(mkDoodad(3, {20.0f, 8.0f, 30.0f}));
        std::vector<WmoDef> none;

        Vec3 pivot = medianPosition(dd, none);
        CHECK_NEAR(pivot.x, 10.0f, 0.01);
        CHECK_NEAR(pivot.y,  4.0f, 0.01);
        CHECK_NEAR(pivot.z, 20.0f, 0.01);

        std::vector<PlacementClip> clips = copySelection(dd, none);
        CHECK(clips.size() == 3);
        CHECK(!clips[0].isWmo);
        CHECK(clips[1].modelPath == "World/Tree.m2");
        CHECK_NEAR(clips[1].relPos.x, 0.0f, 0.01);       // the median entry
        CHECK_NEAR(clips[1].relPos.y, 0.0f, 0.01);
        CHECK_NEAR(clips[1].relPos.z, 0.0f, 0.01);
        CHECK_NEAR(clips[0].relPos.x, -10.0f, 0.01);
        CHECK_NEAR(clips[2].relPos.x,  10.0f, 0.01);
        CHECK(clips[0].scale == 1.0f);                   // 1024/1024

        // Empty clipboard paste -> empty result.
        UidAllocator uids;
        PasteResult empty = paste({}, PasteMode::AtPoint, {0, 0, 0}, {}, uids,
                                  nullptr, nullptr);
        CHECK(empty.doodads.empty() && empty.wmos.empty());
        CHECK(uids.maxSeen() == 0);                      // no ids consumed
    }

    // =========================================================================
    // Paste determinism + jitter ranges: same seed twice -> identical output;
    // yaw/tilt/scale all inside the configured ranges; scale quantized /1024.
    // =========================================================================
    {
        std::vector<PlacementClip> clips;
        for (int i = 0; i < 50; ++i) {
            PlacementClip c;
            c.isWmo     = false;
            c.modelPath = "World/Tree.m2";
            c.relPos    = { static_cast<float>(i), static_cast<float>(-i), 0.0f };
            clips.push_back(std::move(c));
        }

        PasteParams pp;
        pp.randomRotation = true;  pp.minRotDeg  = -180.0f; pp.maxRotDeg  = 180.0f;
        pp.randomTilt     = true;  pp.minTiltDeg = -5.0f;   pp.maxTiltDeg = 5.0f;
        pp.randomScale    = true;  pp.minScale   = 0.9f;    pp.maxScale   = 1.1f;
        pp.seed           = 1234;

        UidAllocator a1, a2;
        PasteResult r1 = paste(clips, PasteMode::AtPoint, {100.0f, 50.0f, 25.0f},
                               pp, a1, nullptr, nullptr);
        PasteResult r2 = paste(clips, PasteMode::AtPoint, {100.0f, 50.0f, 25.0f},
                               pp, a2, nullptr, nullptr);

        CHECK(r1.doodads.size() == 50 && r1.wmos.empty());
        bool identical = r1.doodads.size() == r2.doodads.size();
        for (size_t i = 0; identical && i < r1.doodads.size(); ++i)
            identical = sameDoodad(r1.doodads[i], r2.doodads[i]);
        CHECK(identical);

        bool ranges = true, quantized = true, uidsOk = true;
        for (size_t i = 0; i < r1.doodads.size(); ++i) {
            const DoodadDef& d = r1.doodads[i];
            ranges = ranges
                  && d.rot[1] >= -180.0f && d.rot[1] <= 180.0f    // yaw
                  && d.rot[0] >= -5.0f   && d.rot[0] <= 5.0f      // pitch
                  && d.rot[2] >= -5.0f   && d.rot[2] <= 5.0f;     // roll
            // scale is a uint16 count of 1/1024 steps by construction; assert
            // it landed inside the jitter window (0.9..1.1 in /1024 steps).
            quantized = quantized && d.scale >= 921 && d.scale <= 1127;
            uidsOk    = uidsOk && d.uniqueId == static_cast<uint32_t>(i + 1);
        }
        CHECK(ranges);
        CHECK(quantized);
        CHECK(uidsOk);

        // A different seed must change the jitter stream.
        PasteParams pp2 = pp;
        pp2.seed = 99;
        UidAllocator a3;
        PasteResult r3 = paste(clips, PasteMode::AtPoint, {100.0f, 50.0f, 25.0f},
                               pp2, a3, nullptr, nullptr);
        CHECK(!sameDoodad(r1.doodads[0], r3.doodads[0]));
    }

    // =========================================================================
    // PasteMode::OnTerrain: each pasted record's Z comes from HeightFn at its
    // own XY (world space); other modes keep the anchor-relative Z.
    // =========================================================================
    {
        HeightFn h = [](float x, float y) { return 0.25f * x + 0.5f * y + 7.0f; };

        std::vector<PlacementClip> clips;
        for (int i = 0; i < 4; ++i) {
            PlacementClip c;
            c.modelPath = "World/Rock.m2";
            c.relPos    = { 5.0f * i, -3.0f * i, 42.0f };   // Z must be overridden
            clips.push_back(std::move(c));
        }

        UidAllocator uids;
        PasteResult r = paste(clips, PasteMode::OnTerrain, {10.0f, 20.0f, 0.0f},
                              {}, uids, h, nullptr);
        CHECK(r.doodads.size() == 4);
        for (const DoodadDef& d : r.doodads) {
            Vec3 w = recordWorld(d);
            // Stored x/z carry the ZEROPOINT offset, so the recovered world XY
            // is only float-accurate to a few millimetres -- compare loosely.
            CHECK_NEAR(w.z, h(w.x, w.y), 0.05);
        }

        // AtPoint keeps the clip's Z (anchor.z + relPos.z), no height sampling.
        UidAllocator uids2;
        PasteResult r2 = paste(clips, PasteMode::AtPoint, {10.0f, 20.0f, 0.0f},
                               {}, uids2, h, nullptr);
        CHECK_NEAR(recordWorld(r2.doodads[0]).z, 42.0f, 0.001);
    }

    // =========================================================================
    // rotateToGround: pasted rotation maps local +Z onto the sampled normal.
    // =========================================================================
    {
        const float s45 = std::sqrt(0.5f);
        Vec3 n45 = { s45, 0.0f, s45 };                      // 45-degree slope
        NormalFn nf = [n45](float, float) { return n45; };

        PlacementClip c;
        c.modelPath = "World/Tree.m2";
        std::vector<PlacementClip> clips{ c };

        PasteParams pp;
        pp.rotateToGround = true;
        UidAllocator uids;
        PasteResult r = paste(clips, PasteMode::AtPoint, {0, 0, 0}, pp, uids,
                              nullptr, nf);
        CHECK(r.doodads.size() == 1);
        CHECK(dot(localUp(r.doodads[0].rot), n45) > 0.999f);

        // With a random yaw on top, local +Z must STILL land on the normal
        // (rotateToGround keeps the jittered yaw, replaces only the tilt).
        PasteParams pp2 = pp;
        pp2.randomRotation = true;
        pp2.seed = 7;
        UidAllocator uids2;
        Vec3 nSkew = normalize(Vec3{1.0f, 1.0f, 2.0f});
        NormalFn nf2 = [nSkew](float, float) { return nSkew; };
        PasteResult r2 = paste(clips, PasteMode::AtPoint, {0, 0, 0}, pp2, uids2,
                               nullptr, nf2);
        CHECK(dot(localUp(r2.doodads[0].rot), nSkew) > 0.999f);
    }

    // =========================================================================
    // snapToGround: Z re-homed, horizontal stored coords bit-identical;
    // alignToNormal keeps yaw and maps local +Z onto the normal.
    // =========================================================================
    {
        HeightFn h = [](float x, float y) { return 0.1f * x - 0.2f * y + 3.0f; };
        const float s45 = std::sqrt(0.5f);
        Vec3 n45 = { 0.0f, s45, s45 };
        NormalFn nf = [n45](float, float) { return n45; };

        std::vector<DoodadDef> dd;
        dd.push_back(mkDoodad(1, {30.0f, -12.0f, 999.0f}));
        dd[0].rot[1] = 30.0f;                               // pre-existing yaw
        float keepX = dd[0].pos[0], keepZ = dd[0].pos[2];

        snapToGround(dd, h, nf, /*alignToNormal=*/true);
        Vec3 w = recordWorld(dd[0]);
        CHECK_NEAR(w.z, h(w.x, w.y), 0.05);
        CHECK(dd[0].pos[0] == keepX);                       // horizontal untouched
        CHECK(dd[0].pos[2] == keepZ);
        CHECK(dd[0].rot[1] == 30.0f);                       // yaw preserved
        CHECK(dot(localUp(dd[0].rot), n45) > 0.999f);

        // WMO overload: rotation-only alignment works the same way.
        std::vector<WmoDef> ww;
        ww.push_back(mkWmo(2, {5.0f, 5.0f, 100.0f}));
        snapToGround(ww, h, nf, true);
        Vec3 ww0 = recordWorld(ww[0]);
        CHECK_NEAR(ww0.z, h(ww0.x, ww0.y), 0.05);
        CHECK(dot(localUp(ww[0].rot), n45) > 0.999f);
    }

    // =========================================================================
    // WMO paste: tilt allowed (rotation only), scale never jittered (the MODF
    // record has no scale field -- enforced by the type), sets carried over.
    // =========================================================================
    {
        PlacementClip c;
        c.isWmo     = true;
        c.modelPath = "Buildings/Keep.wmo";
        c.doodadSet = 3;
        c.nameSet   = 1;
        std::vector<PlacementClip> clips{ c };

        PasteParams pp;
        pp.randomTilt  = true;   pp.minTiltDeg = -5.0f; pp.maxTiltDeg = 5.0f;
        pp.randomScale = true;                           // must be a no-op for WMOs
        pp.seed        = 42;

        UidAllocator uids;
        PasteResult r = paste(clips, PasteMode::AtPoint, {0, 0, 0}, pp, uids,
                              nullptr, nullptr);
        CHECK(r.wmos.size() == 1 && r.doodads.empty());
        const WmoDef& w = r.wmos[0];
        CHECK(w.uniqueId == 1);
        CHECK(w.doodadSet == 3 && w.nameSet == 1);
        CHECK(w.rot[0] >= -5.0f && w.rot[0] <= 5.0f);
        CHECK(w.rot[2] >= -5.0f && w.rot[2] <= 5.0f);
        CHECK(w.rot[1] == 0.0f);                          // no yaw jitter requested
    }

    // =========================================================================
    // replacePreservingTransform: only the model reference changes; every
    // transform byte (pos/rot/scale/uniqueId) stays bit-identical.
    // =========================================================================
    {
        std::vector<DoodadDef> dd;
        dd.push_back(mkDoodad(77, {12.5f, -3.25f, 100.0f}, "World/Tree.m2"));
        dd[0].rot[0] = 1.5f; dd[0].rot[1] = 271.25f; dd[0].rot[2] = -4.75f;
        dd[0].scale  = 1337;
        dd[0].mmidIndex = 4;
        DoodadDef before = dd[0];

        int n = replacePreservingTransform(dd, {0, 99 /*out of range: ignored*/},
                                           "World/DeadTree.m2", 9);
        CHECK(n == 1);
        CHECK(dd[0].modelName == "World/DeadTree.m2");
        CHECK(dd[0].mmidIndex == 9);
        CHECK(dd[0].uniqueId == before.uniqueId);
        CHECK(dd[0].pos[0] == before.pos[0] && dd[0].pos[1] == before.pos[1]
           && dd[0].pos[2] == before.pos[2]);
        CHECK(dd[0].rot[0] == before.rot[0] && dd[0].rot[1] == before.rot[1]
           && dd[0].rot[2] == before.rot[2]);
        CHECK(dd[0].scale == before.scale);
        CHECK(dd[0].flags == before.flags);

        std::vector<WmoDef> ww;
        ww.push_back(mkWmo(5, {1, 2, 3}, "Buildings/Keep.wmo"));
        CHECK(replacePreservingTransform(ww, {0}, "Buildings/Tower.wmo", 2) == 1);
        CHECK(ww[0].modelName == "Buildings/Tower.wmo" && ww[0].mwidIndex == 2);
        CHECK(ww[0].uniqueId == 5);
    }

    // =========================================================================
    // CSV sidecar: golden-string compare, incl. a path containing ';'.
    // =========================================================================
    {
        std::vector<DoodadDef> dd(2);
        dd[0].modelName = "World/Tree.m2";
        dd[0].pos[0] = 1.5f; dd[0].pos[1] = 2.5f; dd[0].pos[2] = 3.5f;
        dd[0].rot[1] = 45.0f;
        dd[0].scale  = 2048;                              // 2.0 normalized
        dd[1].modelName = "World/We;ird.m2";              // separator in path
        dd[1].scale  = 1024;

        std::vector<WmoDef> ww(1);
        ww[0].modelName = "Buildings/Keep.wmo";
        ww[0].pos[0] = -10.25f; ww[0].pos[1] = 20.5f; ww[0].pos[2] = -30.75f;
        ww[0].rot[1] = 90.0f;

        std::string csv = exportPlacementsCsv(dd, ww, nullptr);
        std::string expected =
            "ModelFile;PosX;PosY;PosZ;RotX;RotY;RotZ;Scale;Kind\n"
            "World/Tree.m2;1.5;2.5;3.5;0;45;0;2;m2\n"
            "\"World/We;ird.m2\";0;0;0;0;0;0;1;m2\n"
            "Buildings/Keep.wmo;-10.25;20.5;-30.75;0;90;0;1;wmo\n";
        CHECK(csv == expected);

        // nameResolver fills in entries whose modelName is empty.
        std::vector<DoodadDef> anon(1);
        anon[0].mmidIndex = 7;
        anon[0].scale     = 1024;
        std::string resolved = exportPlacementsCsv(anon, {},
            [](bool isWmo, uint32_t idx) {
                return !isWmo && idx == 7 ? std::string("World/Resolved.m2")
                                          : std::string("wrong");
            });
        CHECK(resolved.find("World/Resolved.m2;") != std::string::npos);
    }
}
