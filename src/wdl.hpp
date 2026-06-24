#pragma once
// ---------------------------------------------------------------------------
// WDL: a map's low-resolution heightfield (one MARE block per ADT tile), used by
// the client for the distant-horizon and the world map. Layout:
//   MVER, MAOF (64*64 uint32 file offsets to each tile's MARE; 0 = absent),
//   MARE per present tile = 17*17 outer int16 heights + 16*16 inner int16.
// We keep the 17x17 outer grid per tile; that is enough for a top-down minimap.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>

namespace wf {

struct Wdl {
    static constexpr int DIM   = 64;   // tiles per side
    static constexpr int OUTER = 17;   // outer grid per tile
    static constexpr int N     = OUTER * OUTER;   // 289

    std::array<bool, DIM * DIM> present{};
    std::vector<int16_t> outer;        // DIM*DIM*N, zero where absent

    bool tilePresent(int x, int y) const {
        return x >= 0 && x < DIM && y >= 0 && y < DIM && present[y * DIM + x];
    }
    // Outer height at tile (x,y), grid cell (i,j) in [0,17). No bounds checks.
    int16_t height(int x, int y, int i, int j) const {
        return outer[(static_cast<size_t>(y) * DIM + x) * N + i * OUTER + j];
    }
    size_t tileCount() const {
        size_t n = 0; for (bool p : present) n += p ? 1 : 0; return n;
    }
};

// Parse a .wdl buffer. Returns an all-absent Wdl if MAOF is missing/too small.
Wdl parseWdl(const std::vector<uint8_t>& buf);

}  // namespace wf
