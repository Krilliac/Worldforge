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

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Canonical vanilla 1.12.1 archive priority (LOW priority first; patches win).
// Locale archives are appended after, with {locale} substituted.
const char* kBaseArchives[] = {
    "base.MPQ", "dbc.MPQ", "interface.MPQ", "misc.MPQ", "model.MPQ",
    "sound.MPQ", "speech.MPQ", "terrain.MPQ", "texture.MPQ", "wmo.MPQ",
    "patch.MPQ", "patch-2.MPQ",
};
const char* kLocaleArchives[] = {
    "locale-{loc}.MPQ", "speech-{loc}.MPQ", "patch-{loc}.MPQ", "patch-{loc}-2.MPQ",
};

std::string replaceLoc(std::string s, const std::string& loc) {
    const std::string token = "{loc}";
    for (size_t p; (p = s.find(token)) != std::string::npos; )
        s.replace(p, token.size(), loc);
    return s;
}

void openChain(wf::MpqManager& mpq, const fs::path& dataDir, const std::string& locale) {
    auto tryOpen = [&](const fs::path& p) {
        if (!fs::exists(p)) return;
        if (mpq.addArchive(p.string()))
            std::printf("  + %s\n", p.filename().string().c_str());
        else
            std::printf("  ! failed to open %s\n", p.string().c_str());
    };
    for (const char* a : kBaseArchives)   tryOpen(dataDir / a);
    const fs::path locDir = dataDir / locale;
    for (const char* a : kLocaleArchives) tryOpen(locDir / replaceLoc(a, locale));
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <DataDir> <MapName> [tileX tileY] [--locale enUS]\n", argv[0]);
        return 2;
    }

    const fs::path dataDir = argv[1];
    const std::string map  = argv[2];

    int  onlyX = -1, onlyY = -1;
    bool single = false;
    std::string locale = "enUS";

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--locale" && i + 1 < argc) {
            locale = argv[++i];
        } else if (!single && i + 1 < argc &&
                   a.find_first_not_of("0123456789") == std::string::npos) {
            onlyX  = std::stoi(a);
            onlyY  = std::stoi(argv[++i]);
            single = true;
        }
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
    } else {
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (wdt.hasTile(x, y))
                    dumpTile(mpq, map, x, y);
    }
    return 0;
}
