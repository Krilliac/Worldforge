// ---------------------------------------------------------------------------
// wforge-flythrough : offline multi-tile map overview.
//
//   Loads an NxN block of real ADT tiles from a vanilla 1.12.1 client, stitches
//   them into one world-space scene (every tile's geometry is already baked into
//   WoW world coordinates), frames a camera over the whole block, and renders a
//   single overview PNG -- with NO world server. Proves the offline "explore a
//   map" path spanning multiple tiles, on top of the per-tile renderer.
//
//   Usage:
//     wforge-flythrough <ClientDir|Data|.> <Map> <centerX> <centerY> [N] [out.png]
//
//   Example:
//     wforge-flythrough "D:/World of Warcraft Classic 1.12.1" Azeroth 32 48 2 out.png
// ---------------------------------------------------------------------------
#include "mpq.hpp"
#include "client_data.hpp"
#include "wow_files.hpp"
#include "asset_loader.hpp"
#include "lighting.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Parse DBFilesClient\<name>.dbc from the chain; empty Dbc if absent/malformed.
static wf::Dbc loadDbc(const wf::MpqManager& mpq, const std::string& name) {
    std::vector<uint8_t> buf; wf::Dbc d;
    if (mpq.readFile("DBFilesClient\\" + name + ".dbc", buf)) {
        try { d = wf::Dbc::parse(buf); } catch (...) {}
    }
    return d;
}

// Accumulate the world-space AABB of a tile's terrain into lo/hi.
static void accumulateBounds(const wf::TileRender& t, wf::Vec3& lo, wf::Vec3& hi) {
    for (const auto& m : t.chunkMeshes)
        for (const auto& v : m.vertices) {
            lo.x = std::min(lo.x, v.position.x); lo.y = std::min(lo.y, v.position.y); lo.z = std::min(lo.z, v.position.z);
            hi.x = std::max(hi.x, v.position.x); hi.y = std::max(hi.y, v.position.y); hi.z = std::max(hi.z, v.position.z);
        }
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
            "usage: %s <ClientDir|Data|.> <Map> <centerX> <centerY> [N=2] [out.png]\n", argv[0]);
        return 2;
    }
    const fs::path dataHint = argv[1];
    const std::string map = argv[2];
    const int cx = std::atoi(argv[3]);
    const int cy = std::atoi(argv[4]);
    const int N  = (argc > 5) ? std::max(1, std::atoi(argv[5])) : 2;
    const std::string outPng = (argc > 6) ? argv[6] : "worldforge_flythrough.png";

    fs::path dataDir = wf::findDataDir(dataHint);
    if (dataDir.empty()) dataDir = dataHint;
    std::string locale = wf::detectLocale(dataDir);
    if (locale.empty()) locale = "enUS";

    wf::MpqManager mpq;
    wf::mountWowClient(mpq, dataDir, locale);
    if (mpq.archiveCount() == 0) { std::fprintf(stderr, "No archives opened (Data dir?)\n"); return 1; }

    wf::AssetLoader loader(mpq);
    wf::Wdt wdt;
    if (!loader.loadWdt(map, wdt)) { std::fprintf(stderr, "WDT not found for map %s\n", map.c_str()); return 1; }

    // Build the Light.dbc database so tiles render with zone-appropriate lighting
    // (a no-op fallback to the legacy grey light if the tables are missing).
    wf::Dbc light = loadDbc(mpq, "Light"), lparams = loadDbc(mpq, "LightParams");
    wf::Dbc lint = loadDbc(mpq, "LightIntBand"), lfloat = loadDbc(mpq, "LightFloatBand");
    wf::LightDatabase lights;
    lights.build(&light, &lparams, &lint, &lfloat);
    const uint32_t mapId = (map == "Kalimdor") ? 1u : 0u;   // Azeroth=0 default
    if (!lights.empty()) std::printf("  (zone lighting: Light.dbc loaded)\n");

    // Build every present tile in the NxN block; keep them alive and accumulate bounds.
    std::vector<wf::TileScene> scenes;
    wf::Vec3 lo{ 1e30f, 1e30f, 1e30f }, hi{ -1e30f, -1e30f, -1e30f };
    size_t doodads = 0, wmos = 0;
    for (int y = cy; y < cy + N; ++y) {
        for (int x = cx; x < cx + N; ++x) {
            if (!wdt.hasTile(x, y)) continue;
            wf::TileScene s = loader.buildTileScene(map, x, y);
            if (s.terrain.empty()) continue;
            wf::AssetLoader::applyLighting(s, lights, mapId, x, y);   // zone light (noon)
            accumulateBounds(s.terrain, lo, hi);
            doodads += s.doodadCount();
            wmos    += s.wmoCount();
            scenes.push_back(std::move(s));
            std::printf("  + tile %d,%d\n", x, y);
        }
    }
    if (scenes.empty()) { std::fprintf(stderr, "No present/renderable tiles in block.\n"); return 1; }

    // Frame a camera over the whole block's bounds and render every scene into one fb.
    wf::Vec3 c{ (lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f };
    float r = wf::length(hi - c) + 1.0f;

    const int W = 1280, H = 960;
    wf::Framebuffer fb(W, H);
    fb.clear(wf::Rgba{ 24, 28, 40, 255 });
    wf::Mat4 view = wf::Mat4::lookAt(c + wf::Vec3{ r*0.9f, r*0.9f, r*0.85f }, c, { 0, 0, 1 });
    wf::Mat4 proj = wf::Mat4::perspective(55.0, double(W)/H, 1.0, r * 6.0 + 100.0);
    wf::Mat4 vp = proj * view;
    for (const wf::TileScene& s : scenes)
        s.renderLit(fb, vp, wf::Vec3{ 0.5f, 0.4f, 0.8f });   // zone-lit (Light.dbc)

    if (!wf::writePng(fb.color, outPng)) { std::fprintf(stderr, "write failed: %s\n", outPng.c_str()); return 1; }
    std::printf("## flythrough: wrote %s  (%zu tiles, %zu doodads, %zu wmos)\n",
                outPng.c_str(), scenes.size(), doodads, wmos);
    return 0;
}
