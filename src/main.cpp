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
#include "dbc_defs.hpp"
#include "coords.hpp"
#include "terrain.hpp"
#include "asset_loader.hpp"
#include "client_data.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"
#include "wmo.hpp"
#include "wmo_render.hpp"
#include "bounds.hpp"

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

// Validate the placement->world transform and the per-tile world origin on
// REAL data. Three independent cross-checks for tile (x,y):
//   (1) Every MCNK header's own stored world position (offset 0x68) must equal
//       chunkCornerWorld(blockX,blockY,row,col) for the IndexX/IndexY axis map
//       used by terrain.cpp. This pins both the tile origin AND the
//       IndexX=col / IndexY=row assignment flagged as runtime-verify.
//   (2) Every MDDF (doodad) and MODF (WMO) placement, after placementToWorld(),
//       must fall inside this tile's 533.333-yard world square, i.e.
//       tileIndexFromCoord(worldX)==x and ==y.
//   (3) Report a sample raw->world conversion so the axis order is auditable.
void placementCheck(const wf::MpqManager& mpq, const std::string& map, int x, int y) {
    const std::string archived =
        "World\\Maps\\" + map + "\\" + map + "_" +
        std::to_string(x) + "_" + std::to_string(y) + ".adt";
    std::vector<uint8_t> buf;
    if (!mpq.readFile(archived, buf)) {
        std::printf("## placement-check %d,%d : ADT not found\n", x, y);
        return;
    }
    wf::Adt adt;
    std::vector<wf::MapChunk> chunks;
    try {
        adt    = wf::parseAdt(buf);
        chunks = wf::parseChunks(buf);
    } catch (const std::exception& e) {
        std::printf("## placement-check %d,%d : parse error: %s\n", x, y, e.what());
        return;
    }

    // The tile's expected world square. Verified axis identity (see coords.hpp):
    // filename X is the WEST tile index, filename Y is the NORTH tile index.
    const double tileNorthMax = (32.0 - y) * wf::TILE_SIZE;  // X north edge <- fileY
    const double tileWestMax  = (32.0 - x) * wf::TILE_SIZE;  // Y west edge  <- fileX
    std::printf("## placement-check %d,%d  (fileX=west idx, fileY=north idx)\n", x, y);
    std::printf("   tile world square: X north [%.3f .. %.3f]  Y west [%.3f .. %.3f]\n",
                tileNorthMax - wf::TILE_SIZE, tileNorthMax,
                tileWestMax  - wf::TILE_SIZE, tileWestMax);

    // (1) MCNK header position vs computed corner.
    int mcnkOk = 0, mcnkBad = 0; double mcnkMaxErr = 0.0;
    for (const wf::MapChunk& mc : chunks) {
        const int col = static_cast<int>(mc.indexX);  // terrain.cpp axis map
        const int row = static_cast<int>(mc.indexY);
        wf::Vec3 corner = wf::chunkCornerWorld(x, y, row, col, mc.position.z);
        double ex = std::abs(double(corner.x) - double(mc.position.x));
        double ey = std::abs(double(corner.y) - double(mc.position.y));
        double e  = std::max(ex, ey);
        mcnkMaxErr = std::max(mcnkMaxErr, e);
        if (e < 0.05) ++mcnkOk; else {
            if (mcnkBad < 4)
                std::printf("   MCNK idx(%u,%u) header pos=(%.3f,%.3f) "
                            "computed corner=(%.3f,%.3f)  dErr=%.3f\n",
                            mc.indexX, mc.indexY,
                            mc.position.x, mc.position.y, corner.x, corner.y, e);
            ++mcnkBad;
        }
    }
    std::printf("   MCNK origin/axis: %d ok, %d mismatch (max XY err %.4f yd over %zu chunks)\n",
                mcnkOk, mcnkBad, mcnkMaxErr, chunks.size());

    // (2)+(3) placements inside tile bounds.
    int dOk = 0, dBad = 0, wOk = 0, wBad = 0;
    bool sampled = false;
    auto inTile = [&](wf::Vec3 w) {
        // north (w.x) -> fileY index ; west (w.y) -> fileX index.
        return wf::tileIndexFromCoord(w.x) == y && wf::tileIndexFromCoord(w.y) == x;
    };
    for (const wf::DoodadDef& d : adt.doodads) {
        wf::Vec3 w = wf::placementToWorld(wf::Vec3{ d.pos[0], d.pos[1], d.pos[2] });
        if (!sampled) {
            std::printf("   sample M2 raw=(%.3f,%.3f,%.3f) -> world(N,W,Up)="
                        "(%.3f,%.3f,%.3f)  tile=(%d,%d)\n",
                        d.pos[0], d.pos[1], d.pos[2], w.x, w.y, w.z,
                        wf::tileIndexFromCoord(w.x), wf::tileIndexFromCoord(w.y));
            sampled = true;
        }
        if (inTile(w)) ++dOk; else {
            if (dBad < 3)
                std::printf("   M2 OUTSIDE uid=%u world=(%.1f,%.1f) tile=(%d,%d)\n",
                            d.uniqueId, w.x, w.y,
                            wf::tileIndexFromCoord(w.x), wf::tileIndexFromCoord(w.y));
            ++dBad;
        }
    }
    for (const wf::WmoDef& wo : adt.wmos) {
        wf::Vec3 w = wf::placementToWorld(wf::Vec3{ wo.pos[0], wo.pos[1], wo.pos[2] });
        if (inTile(w)) ++wOk; else {
            if (wBad < 3)
                std::printf("   WMO OUTSIDE uid=%u world=(%.1f,%.1f) tile=(%d,%d)\n",
                            wo.uniqueId, w.x, w.y,
                            wf::tileIndexFromCoord(w.x), wf::tileIndexFromCoord(w.y));
            ++wBad;
        }
    }
    std::printf("   placements in tile: M2 %d/%d, WMO %d/%d "
                "(outside counts may be legal for objects straddling the edge)\n",
                dOk, dOk + dBad, wOk, wOk + wBad);
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

// Read DBFilesClient\<Name>.dbc from the MPQ chain and dump the parsed header
// plus a handful of typed records, proving the WDBC header math + the per-table
// field offsets in dbc_defs.* are correct on real vanilla 1.12 DBCs.
int dumpDbc(const wf::MpqManager& mpq, const std::string& name) {
    const std::string archived = "DBFilesClient\\" + name + ".dbc";
    std::vector<uint8_t> buf;
    if (!mpq.readFile(archived, buf)) {
        std::fprintf(stderr, "DBC not found in archives: %s\n", archived.c_str());
        return 1;
    }

    wf::Dbc dbc;
    try {
        dbc = wf::Dbc::parse(buf);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "DBC parse error (%s): %s\n", name.c_str(), e.what());
        return 1;
    }

    std::printf("# DBC %s.dbc  bytes=%zu  records=%u  fields=%u  recordSize=%u\n",
                name.c_str(), buf.size(), dbc.recordCount(),
                dbc.fieldCount(), dbc.recordSize());
    if (dbc.fieldCount() * 4 != dbc.recordSize())
        std::printf("  ! warning: fieldCount*4 (%u) != recordSize (%u)\n",
                    dbc.fieldCount() * 4, dbc.recordSize());

    const uint32_t maxShow = 16;   // first N records as a spot check

    if (name == "Map") {
        for (uint32_t i = 0; i < dbc.recordCount() && i < maxShow; ++i) {
            wf::MapEntry e = wf::mapEntry(dbc, i);
            std::printf("Map id=%-4u dir=%-20s type=%u name=\"%s\"\n",
                        e.id, e.directory.c_str(), e.instanceType, e.name.c_str());
        }
    } else if (name == "AreaTable") {
        for (uint32_t i = 0; i < dbc.recordCount() && i < maxShow; ++i) {
            wf::AreaEntry e = wf::areaEntry(dbc, i);
            std::printf("Area id=%-5u map=%-3u parent=%-5u explLvl=%-3d name=\"%s\"\n",
                        e.id, e.mapId, e.parentAreaId, e.explorationLevel,
                        e.name.c_str());
        }
    } else if (name == "LiquidType") {
        for (uint32_t i = 0; i < dbc.recordCount() && i < maxShow; ++i) {
            wf::LiquidTypeEntry e = wf::liquidTypeEntry(dbc, i);
            std::printf("Liquid id=%-3u liquidId=%-3u type=%u spell=%u\n",
                        e.id, e.liquidId, e.type, e.spellId);
        }
    } else if (name == "Light") {
        for (uint32_t i = 0; i < dbc.recordCount() && i < maxShow; ++i) {
            wf::LightEntry e = wf::lightEntry(dbc, i);
            std::printf("Light id=%-4u map=%-3u pos=(%.1f, %.1f, %.1f) "
                        "falloff=(%.1f..%.1f) params[0]=%u\n",
                        e.id, e.mapId, e.x, e.y, e.z,
                        e.falloffStart, e.falloffEnd, e.lightParams[0]);
        }
    } else {
        std::printf("# (no typed view for '%s' -- header parsed OK, %u records)\n",
                    name.c_str(), dbc.recordCount());
    }
    return 0;
}

// Load a real WMO (root + its `_NNN.wmo` group files) straight from the MPQ
// chain, build the textured render parts (MOPY material batching) and frame a
// camera to the WMO bounds, then render to a PNG. Proves the WMO root + group
// parse, MOMT/MOBA material batching and group geometry are correct on real
// buildings -- and dumps the structural counts so they can be cross-checked.
int renderWmo(const wf::MpqManager& mpq, const std::string& wmoPath,
              const std::string& outPng) {
    wf::AssetLoader loader(mpq);
    std::shared_ptr<const wf::WmoModel> wm = loader.wmo(wmoPath);
    if (!wm) {
        std::fprintf(stderr, "WMO not found / failed to parse: %s\n", wmoPath.c_str());
        return 1;
    }
    const wf::WmoRoot& root = wm->root;

    // ---- structural report (root) ----
    std::printf("# WMO %s\n", wmoPath.c_str());
    std::printf("#   root: textures=%u materials=%zu groups(MOHD)=%u groups(loaded)=%zu "
                "doodadSets=%zu doodadDefs=%zu  flags=0x%04X\n",
                root.nTextures, root.materials.size(), root.nGroups, wm->groups.size(),
                root.doodadSets.size(), root.doodads.size(), root.flags);
    std::printf("#   root bbox min=(%.2f, %.2f, %.2f) max=(%.2f, %.2f, %.2f)\n",
                root.bboxMin.x, root.bboxMin.y, root.bboxMin.z,
                root.bboxMax.x, root.bboxMax.y, root.bboxMax.z);

    // ---- per-group geometry / batch totals ----
    size_t totVtx = 0, totTri = 0, totBatch = 0, totIdx = 0;
    for (size_t gi = 0; gi < wm->groups.size(); ++gi) {
        const wf::WmoGroup& g = wm->groups[gi];
        size_t tris = g.indices.size() / 3;
        totVtx += g.vertices.size();
        totTri += tris;
        totIdx += g.indices.size();
        totBatch += g.batches.size();
        std::printf("#   group[%zu] flags=0x%08X verts=%zu tris=%zu indices=%zu "
                    "normals=%zu uvs=%zu MOPY=%zu MOBA=%zu\n",
                    gi, g.flags, g.vertices.size(), tris, g.indices.size(),
                    g.normals.size(), g.uvs.size(), g.triMaterial.size(),
                    g.batches.size());
        // Sanity: every MOVI index must be in range and counts must agree.
        if (!g.indices.empty()) {
            uint16_t mx = 0;
            for (uint16_t idx : g.indices) mx = std::max(mx, idx);
            if (mx >= g.vertices.size())
                std::printf("#     ! group[%zu]: max MOVI index %u >= vertex count %zu\n",
                            gi, mx, g.vertices.size());
        }
        if (!g.triMaterial.empty() && g.triMaterial.size() != tris)
            std::printf("#     ! group[%zu]: MOPY tri-count %zu != indices/3 %zu\n",
                        gi, g.triMaterial.size(), tris);
    }
    std::printf("#   TOTALS verts=%zu tris=%zu indices=%zu batches=%zu\n",
                totVtx, totTri, totIdx, totBatch);

    // ---- material / blend-mode breakdown ----
    for (size_t mi = 0; mi < root.materials.size(); ++mi) {
        const wf::WmoMaterial& m = root.materials[mi];
        std::printf("#   mat[%zu] shader=%u blend=%u flags=0x%08X tex=\"%s\"\n",
                    mi, m.shader, m.blendMode, m.flags, m.diffuseTexture.c_str());
    }

    // ---- build render parts (MOPY -> per-material TexMeshes) ----
    std::vector<wf::WmoRenderPart> parts = wf::wmoRenderParts(*wm);
    size_t partTris = 0;
    for (const wf::WmoRenderPart& p : parts) partTris += p.mesh.indices.size() / 3;
    std::printf("#   render parts=%zu (triangles=%zu)\n", parts.size(), partTris);

    // ---- frame a camera to the group geometry bounds ----
    wf::Aabb box = wf::wmoBounds(*wm);
    wf::Vec3 lo = box.min, hi = box.max;
    if (!(lo.x <= hi.x)) { lo = root.bboxMin; hi = root.bboxMax; }   // fall back to root bbox
    wf::Vec3 c{ (lo.x+hi.x)*0.5f, (lo.y+hi.y)*0.5f, (lo.z+hi.z)*0.5f };
    float r = wf::length(hi - c) + 1.0f;

    const int W = 1024, H = 768;
    wf::Framebuffer fb(W, H);
    fb.clear(wf::Rgba{ 24, 28, 40, 255 });
    wf::Mat4 view = wf::Mat4::lookAt(c + wf::Vec3{ r*0.9f, r*0.7f, r*0.6f }, c, { 0, 0, 1 });
    wf::Mat4 proj = wf::Mat4::perspective(50.0, double(W)/H, r*0.02 + 0.1, r*6.0 + 100.0);
    wf::Mat4 mvp  = proj * view;
    const wf::Vec3 lightDir{ 0.5f, 0.4f, 0.8f };

    // Draw opaque/alpha-test parts first, then alpha-blended (parts are pre-sorted).
    size_t drawn = 0;
    for (const wf::WmoRenderPart& p : parts) {
        if (p.mesh.indices.empty()) continue;
        // texture() returns the magenta fallback for an empty/missing path, so
        // we never have to touch the private fallback() directly.
        std::shared_ptr<const wf::Image> tex = loader.texture(p.texture);
        bool blended = p.blendMode >= 2;
        wf::rasterTexMesh(fb, p.mesh, mvp, *tex, lightDir, blended);
        ++drawn;
    }

    if (!wf::writePng(fb.color, outPng)) {
        std::fprintf(stderr, "render: write failed\n");
        return 1;
    }
    std::printf("## wmo render: wrote %s  (%zu parts drawn, %zu groups, %zu tris)\n",
                outPng.c_str(), drawn, wm->groups.size(), totTri);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <DataDir|WoWInstallDir|.> <MapName> [tileX tileY] "
            "[--locale enUS] [--render out.png]\n"
            "       %s <DataDir|WoWInstallDir|.> --dbc <Name>   "
            "(Name in {Map,AreaTable,LiquidType,Light,...})\n"
            "       %s <DataDir|WoWInstallDir|.> --wmo <archived\\path.wmo> out.png\n"
            "  The first argument may be a Data dir, a WoW install root (next to\n"
            "  WoW.exe), or '.' to auto-detect from the current directory.\n",
            argv[0], argv[0], argv[0]);
        return 2;
    }

    const fs::path dataHint = argv[1];
    // argv[2] is the MapName, unless argv[2] is the --dbc flag (DBC dump mode).
    const std::string arg2 = argv[2];
    const std::string map  = (arg2 == "--dbc" || arg2 == "--wmo") ? std::string() : arg2;

    int  onlyX = -1, onlyY = -1;
    bool single = false;
    std::string locale;            // empty -> auto-detect
    std::string renderPng;
    std::string dbcName;           // non-empty -> DBC dump mode
    std::string wmoPath, wmoOut;   // wmoPath non-empty -> WMO render mode
    bool placementCheckMode = false;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dbc" && i + 1 < argc) {
            dbcName = argv[++i];
        } else if (a == "--wmo" && i + 2 < argc) {
            wmoPath = argv[++i];
            wmoOut  = argv[++i];
        } else if (a == "--locale" && i + 1 < argc) {
            locale = argv[++i];
        } else if (a == "--placement-check") {
            placementCheckMode = true;
        } else if (a == "--render" && i + 1 < argc) {
            renderPng = argv[++i];
        } else if (i >= 3 && !single && i + 1 < argc &&
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

    // DBC dump mode: read DBFilesClient\<Name>.dbc and dump parsed records.
    if (!dbcName.empty())
        return dumpDbc(mpq, dbcName);

    // WMO render mode: load a real building (root + groups), report structural
    // counts and render it to a PNG. No map / WDT needed.
    if (!wmoPath.empty())
        return renderWmo(mpq, wmoPath, wmoOut);

    if (map.empty()) {
        std::fprintf(stderr, "No map name given (and no --dbc <Name>).\n");
        return 2;
    }

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
    std::printf("# WDT MPHD flags=0x%X  -> alpha format: %s\n",
                wdt.mphdFlags, wdt.bigAlpha() ? "8-bit big alpha (4096 B)"
                                              : "packed 4-bit (2048 B)");

    if (single) {
        if (!wdt.hasTile(onlyX, onlyY))
            std::printf("# Note: WDT marks tile %d,%d as absent; trying anyway.\n",
                        onlyX, onlyY);
        if (placementCheckMode) placementCheck(mpq, map, onlyX, onlyY);
        else {
            dumpTile(mpq, map, onlyX, onlyY);
            if (!renderPng.empty()) renderTile(mpq, map, onlyX, onlyY, renderPng);
        }
    } else if (placementCheckMode) {
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (wdt.hasTile(x, y))
                    placementCheck(mpq, map, x, y);
    } else {
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (wdt.hasTile(x, y))
                    dumpTile(mpq, map, x, y);
    }
    return 0;
}
