// ---------------------------------------------------------------------------
// wforge-dump : P0 acceptance tool.
//
//   Opens a vanilla 1.12.1 client's MPQ chain, reads a map's WDT to find which
//   ADT tiles exist, then dumps every doodad (M2) and map-object (WMO)
//   placement as text. RAW stored coordinates -- no world transform yet (P1).
//
//   Usage:
//     wforge-dump <DataDir> <MapName> [tileX tileY] [--locale enUS]
//
//   Examples:
//     wforge-dump "C:/Games/WoW/Data" Azeroth
//     wforge-dump "C:/Games/WoW/Data" Kalimdor 32 48 --locale enUS
//
//   Acceptance test: the dumped model names + uniqueIds + raw positions must
//   match what the same tiles show in Noggit / the live client. If a tile is
//   flagged present in the WDT but its ADT won't resolve, you'll get a warning
//   -- the usual cause is an X/Y filename-order swap, which this surfaces
//   immediately rather than hiding.
// ---------------------------------------------------------------------------
#include "mpq.hpp"
#include "wow_files.hpp"
#include "coords.hpp"
#include "asset_loader.hpp"
#include "client_data.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void openChain(wf::MpqManager& mpq, const fs::path& dataDir, const std::string& locale) {
    wf::mountWowClient(mpq, dataDir, locale, [](const std::string& path, bool ok) {
        const fs::path p = path;
        if (ok) std::printf("  + %s\n", p.filename().string().c_str());
        else    std::printf("  ! failed to open %s\n", path.c_str());
    });
}

void dumpTile(const wf::MpqManager& mpq, const std::string& map, int x, int y) {
    char adtName[64];
    std::snprintf(adtName, sizeof(adtName), "%s_%d_%d.adt", map.c_str(), x, y);
    const std::string archived = "World\\Maps\\" + map + "\\" + adtName;

    std::vector<uint8_t> buf;
    if (!mpq.readFile(archived, buf)) {
        std::printf("## Tile %d,%d  %s  -- NOT FOUND in archives "
                    "(WDT says it exists; check X/Y order)\n", x, y, adtName);
        return;
    }

    wf::Adt adt;
    try {
        adt = wf::parseAdt(buf);
    } catch (const std::exception& e) {
        std::printf("## Tile %d,%d  %s  -- PARSE ERROR: %s\n", x, y, adtName, e.what());
        return;
    }

    std::printf("## Tile %d,%d  %s  (doodads: %zu, wmos: %zu)\n",
                x, y, adtName, adt.doodads.size(), adt.wmos.size());

    for (const wf::DoodadDef& d : adt.doodads) {
        std::printf("M2  uid=%-8u %-48s pos=(%.3f, %.3f, %.3f) "
                    "rot=(%.1f, %.1f, %.1f) scale=%.3f\n",
                    d.uniqueId, d.modelName.c_str(),
                    d.pos[0], d.pos[1], d.pos[2],
                    d.rot[0], d.rot[1], d.rot[2],
                    d.scale / 1024.0);
    }
    for (const wf::WmoDef& w : adt.wmos) {
        std::printf("WMO uid=%-8u %-48s pos=(%.3f, %.3f, %.3f) "
                    "rot=(%.1f, %.1f, %.1f) set=%u/%u\n",
                    w.uniqueId, w.modelName.c_str(),
                    w.pos[0], w.pos[1], w.pos[2],
                    w.rot[0], w.rot[1], w.rot[2],
                    w.doodadSet, w.nameSet);
    }
}

// Build a single tile's textured terrain and render it to a PNG, framing the
// whole tile. Real proof of the MPQ -> parse -> textured render pipeline.
bool renderTile(const wf::MpqManager& mpq, const std::string& map, int x, int y,
                const std::string& outPng) {
    wf::AssetLoader loader(mpq);
    wf::TileScene scene = loader.buildTileScene(map, x, y);
    if (scene.terrain.empty()) { std::printf("## render: tile %d,%d empty / not found\n", x, y); return false; }
    const wf::TileRender& tile = scene.terrain;

    // Fit a camera to the tile's vertex bounds.
    wf::Vec3 lo{ +1e30f, +1e30f, +1e30f }, hi{ -1e30f, -1e30f, -1e30f };
    for (const auto& m : tile.chunkMeshes)
        for (const auto& v : m.vertices) {
            lo.x = std::min(lo.x, v.position.x); lo.y = std::min(lo.y, v.position.y); lo.z = std::min(lo.z, v.position.z);
            hi.x = std::max(hi.x, v.position.x); hi.y = std::max(hi.y, v.position.y); hi.z = std::max(hi.z, v.position.z);
        }
    wf::Vec3 c{ (lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f };
    float r = wf::length(hi - c) + 1.0f;

    const int W = 1024, H = 768;
    wf::Framebuffer fb(W, H);
    fb.clear(wf::Rgba{ 24, 28, 40, 255 });
    wf::Mat4 view = wf::Mat4::lookAt(c + wf::Vec3{ r*0.9f, r*0.9f, r*0.8f }, c, { 0, 0, 1 });
    wf::Mat4 proj = wf::Mat4::perspective(55.0, double(W)/H, 1.0, r * 6.0 + 100.0);
    scene.render(fb, proj * view, wf::Vec3{ 0.5f, 0.4f, 0.8f });

    if (!wf::writePng(fb.color, outPng)) { std::fprintf(stderr, "render: write failed\n"); return false; }
    std::printf("## render: wrote %s  (%zu chunks, %zu textures, %zu doodads)\n",
                outPng.c_str(), tile.chunkMeshes.size(), tile.textures.size(),
                scene.doodadCount());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <DataDir|WoWInstallDir|.> <MapName> [tileX tileY] "
            "[--locale enUS] [--render out.png]\n"
            "  The first argument may be a Data dir, a WoW install root (next to\n"
            "  WoW.exe), or '.' to auto-detect from the current directory.\n", argv[0]);
        return 2;
    }

    const fs::path dataHint = argv[1];
    const std::string map  = argv[2];

    int  onlyX = -1, onlyY = -1;
    bool single = false;
    std::string locale;            // empty -> auto-detect
    std::string renderPng;

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--locale" && i + 1 < argc) {
            locale = argv[++i];
        } else if (a == "--render" && i + 1 < argc) {
            renderPng = argv[++i];
        } else if (!single && i + 1 < argc &&
                   a.find_first_not_of("0123456789") == std::string::npos) {
            onlyX  = std::stoi(a);
            onlyY  = std::stoi(argv[++i]);
            single = true;
        }
    }

    // Resolve the Data directory: accept a Data dir, a WoW install root (next to
    // WoW.exe), or any ancestor of one. Fall back to the literal hint so the
    // "no archives" error below still reports something useful.
    fs::path dataDir = wf::findDataDir(dataHint);
    if (dataDir.empty()) dataDir = dataHint;
    else if (dataDir != dataHint)
        std::printf("Auto-detected Data dir: %s\n", dataDir.string().c_str());

    if (locale.empty()) {
        locale = wf::detectLocale(dataDir);
        if (locale.empty()) locale = "enUS";   // sensible default
        else std::printf("Auto-detected locale: %s\n", locale.c_str());
    }

    std::printf("Opening archive chain from %s (locale %s):\n",
                dataDir.string().c_str(), locale.c_str());
    wf::MpqManager mpq;
    openChain(mpq, dataDir, locale);
    if (mpq.archiveCount() == 0) {
        std::fprintf(stderr, "No archives opened. Is the Data dir correct?\n");
        return 1;
    }
    std::printf("Opened %zu archive(s).\n\n", mpq.archiveCount());

    const std::string wdtPath = "World\\Maps\\" + map + "\\" + map + ".wdt";
    std::vector<uint8_t> wdtBuf;
    if (!mpq.readFile(wdtPath, wdtBuf)) {
        std::fprintf(stderr, "WDT not found: %s\n", wdtPath.c_str());
        return 1;
    }

    wf::Wdt wdt;
    try {
        wdt = wf::parseWdt(wdtBuf);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "WDT parse error: %s\n", e.what());
        return 1;
    }

    if (wdt.globalWmo) {
        std::printf("# Map '%s' is a global-WMO map (no ADT terrain tiles).\n",
                    map.c_str());
        return 0;
    }

    size_t present = 0;
    for (bool t : wdt.tiles) present += t ? 1 : 0;
    std::printf("# Map: %s   (ADT tiles present: %zu)\n", map.c_str(), present);

    if (single) {
        if (!wdt.hasTile(onlyX, onlyY))
            std::printf("# Note: WDT marks tile %d,%d as absent; trying anyway.\n",
                        onlyX, onlyY);
        dumpTile(mpq, map, onlyX, onlyY);
        if (!renderPng.empty()) renderTile(mpq, map, onlyX, onlyY, renderPng);
    } else {
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (wdt.hasTile(x, y))
                    dumpTile(mpq, map, x, y);
    }
    return 0;
}
