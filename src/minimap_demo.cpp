// ---------------------------------------------------------------------------
// wforge-minimap : whole-map low-res overview from the .wdl heightfield.
//
//   Reads World\Maps\<Map>\<Map>.wdl, parses its per-tile 17x17 outer heights,
//   and renders a north-up, top-down elevation minimap PNG -- the entire
//   continent silhouette at a glance, with no ADT/world-server data.
//
//   Usage: wforge-minimap <ClientDir|Data|.> <Map> [out.png] [cell=10]
// ---------------------------------------------------------------------------
#include "mpq.hpp"
#include "client_data.hpp"
#include "wdl.hpp"
#include "image.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Elevation ramp t in [0,1]: deep blue -> green -> brown -> white.
static wf::Rgba ramp(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto lerp = [](wf::Rgba a, wf::Rgba b, float u) {
        auto m = [&](uint8_t x, uint8_t y) { return (uint8_t)(x + (y - x) * u + 0.5f); };
        return wf::Rgba{ m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255 };
    };
    const wf::Rgba c0{ 30, 60, 120, 255 }, c1{ 60, 130, 70, 255 },
                   c2{ 120, 100, 60, 255 }, c3{ 240, 240, 245, 255 };
    if (t < 0.34f) return lerp(c0, c1, t / 0.34f);
    if (t < 0.67f) return lerp(c1, c2, (t - 0.34f) / 0.33f);
    return lerp(c2, c3, (t - 0.67f) / 0.33f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <ClientDir|Data|.> <Map> [out.png] [cell=10]\n", argv[0]);
        return 2;
    }
    const fs::path dataHint = argv[1];
    const std::string map = argv[2];
    const std::string outPng = (argc > 3) ? argv[3] : "worldforge_minimap.png";
    const int S = (argc > 4) ? std::max(1, std::atoi(argv[4])) : 10;

    fs::path dataDir = wf::findDataDir(dataHint);
    if (dataDir.empty()) dataDir = dataHint;
    std::string locale = wf::detectLocale(dataDir);
    if (locale.empty()) locale = "enUS";

    wf::MpqManager mpq;
    wf::mountWowClient(mpq, dataDir, locale);
    if (mpq.archiveCount() == 0) { std::fprintf(stderr, "No archives opened (Data dir?)\n"); return 1; }

    std::vector<uint8_t> buf;
    const std::string wdlPath = "World\\Maps\\" + map + "\\" + map + ".wdl";
    if (!mpq.readFile(wdlPath, buf)) { std::fprintf(stderr, "WDL not found: %s\n", wdlPath.c_str()); return 1; }
    wf::Wdl wdl = wf::parseWdl(buf);
    if (wdl.tileCount() == 0) { std::fprintf(stderr, "WDL has no tiles.\n"); return 1; }

    // Height range over all present tiles (for the colour ramp).
    int16_t hmin = 32767, hmax = -32768;
    for (int ty = 0; ty < wf::Wdl::DIM; ++ty)
        for (int tx = 0; tx < wf::Wdl::DIM; ++tx)
            if (wdl.tilePresent(tx, ty))
                for (int i = 0; i < wf::Wdl::N; ++i) {
                    int16_t h = wdl.outer[(static_cast<size_t>(ty) * wf::Wdl::DIM + tx) * wf::Wdl::N + i];
                    hmin = std::min(hmin, h); hmax = std::max(hmax, h);
                }
    const float span = std::max(1.0f, float(hmax - hmin));

    const int W = wf::Wdl::DIM * S, H = wf::Wdl::DIM * S;
    wf::Image img(W, H);
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            const int tx = px / S, ty = py / S;
            if (!wdl.tilePresent(tx, ty)) { img.at(px, py) = wf::Rgba{ 16, 28, 48, 255 }; continue; }
            const int i = std::min(wf::Wdl::OUTER - 1, (py % S) * wf::Wdl::OUTER / S);
            const int j = std::min(wf::Wdl::OUTER - 1, (px % S) * wf::Wdl::OUTER / S);
            const float t = (wdl.height(tx, ty, i, j) - hmin) / span;
            img.at(px, py) = ramp(t);
        }

    if (!wf::writePng(img, outPng)) { std::fprintf(stderr, "write failed: %s\n", outPng.c_str()); return 1; }
    std::printf("## minimap: wrote %s  (%zu tiles, height %d..%d)\n",
                outPng.c_str(), wdl.tileCount(), hmin, hmax);
    return 0;
}
