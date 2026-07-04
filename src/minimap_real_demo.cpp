// ---------------------------------------------------------------------------
// wforge-minimap-real : the client's REAL baked minimap for a map, stitched
// from its per-tile BLPs (vs the WDL-heightfield silhouette of wforge-minimap).
//
//   Reads textures\Minimap\md5translate.(trs|txt), resolves every
//   <Map>\map<x>_<y>.blp to its stored hash, decodes each and blits it into one
//   image at tile coordinates.
//
//   Usage: wforge-minimap-real <ClientDir|Data|.> <Map> [out.png] [tilePx=24]
// ---------------------------------------------------------------------------
#include "client_data.hpp"
#include "image.hpp"
#include "minimap.hpp"
#include "mpq.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <ClientDir|Data|.> <Map> [out.png] [tilePx=24]\n", argv[0]);
        return 2;
    }
    const fs::path dataHint = argv[1];
    const std::string map = argv[2];
    const std::string outPng = (argc > 3) ? argv[3] : "worldforge_minimap_real.png";
    const int tilePx = (argc > 4) ? std::max(1, std::atoi(argv[4])) : 24;

    fs::path dataDir = wf::findDataDir(dataHint);
    if (dataDir.empty()) dataDir = dataHint;
    std::string locale = wf::detectLocale(dataDir);
    if (locale.empty()) locale = "enUS";

    wf::MpqManager mpq;
    wf::mountWowClient(mpq, dataDir, locale);
    if (mpq.archiveCount() == 0) { std::fprintf(stderr, "No archives opened (Data dir?)\n"); return 1; }

    // The translate table lives under textures\Minimap; vanilla names it .trs,
    // older builds .txt.
    std::vector<uint8_t> trs;
    if (!mpq.readFile("textures\\Minimap\\md5translate.trs", trs) &&
        !mpq.readFile("textures\\Minimap\\md5translate.txt", trs)) {
        std::fprintf(stderr, "md5translate not found in the mounted client.\n");
        return 1;
    }
    wf::MinimapIndex index = wf::MinimapIndex::parse(std::string(trs.begin(), trs.end()));
    std::printf("[minimap-real] md5translate: %zu tile entries\n", index.size());

    int placed = 0;
    wf::Image img = wf::assembleMinimap(mpq, index, map, tilePx, wf::Rgba{ 16, 20, 30, 255 }, &placed);
    if (placed == 0) {
        std::fprintf(stderr, "No minimap tiles resolved for map '%s' "
                             "(is the name right? e.g. Azeroth / Kalimdor)\n", map.c_str());
        return 1;
    }
    if (!wf::writePng(img, outPng)) { std::fprintf(stderr, "write failed: %s\n", outPng.c_str()); return 1; }
    std::printf("## minimap-real: wrote %s  (%d tiles @ %dpx, %dx%d)\n",
                outPng.c_str(), placed, tilePx, img.width, img.height);
    return 0;
}
