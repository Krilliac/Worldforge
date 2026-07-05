#pragma once
// ---------------------------------------------------------------------------
// WDL: a map's low-resolution heightfield (one MARE block per ADT tile), used by
// the client for the distant-horizon and the world map. Layout:
//   MVER, MAOF (64*64 uint32 file offsets to each tile's MARE; 0 = absent),
//   MARE per present tile = 17*17 outer int16 heights + 16*16 inner int16,
//   followed by an optional MAHO = uint16[16] hole mask (array index = chunk
//   row, bit index = chunk column; a set bit drops that MCNK from the horizon
//   mesh). Pre-WotLK writers omit MAHO; a missing chunk means "no holes".
// We keep the 17x17 outer grid + hole mask per tile; that is enough for a
// top-down minimap and a hole-aware horizon mesh.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>

namespace wf {

struct Wdl {
    static constexpr int DIM   = 64;   // tiles per side
    static constexpr int OUTER = 17;   // outer grid per tile
    static constexpr int N     = OUTER * OUTER;   // 289
    static constexpr int HOLE_ROWS = 16;          // MAHO rows per tile (uint16 each)

    std::array<bool, DIM * DIM> present{};
    std::vector<int16_t> outer;        // DIM*DIM*N, zero where absent

    // Per-tile MAHO hole mask: HOLE_ROWS uint16 rows per tile (row = chunk row,
    // bit = chunk column). All zeros when the file carries no MAHO. May be left
    // empty on hand-built Wdls; the accessors below treat empty as hole-free.
    std::vector<uint16_t> holes;       // DIM*DIM*HOLE_ROWS, zero where absent

    // Absolute byte offset of each tile's 32-byte MAHO payload within the
    // source .wdl buffer parseWdl() read (0 if the tile is absent or the file
    // lacks the chunk). Lets writeWdlHoles() patch edits back in place without
    // rewriting the rest of the file. Not part of the rendered model.
    std::vector<uint32_t> mahoOffset;  // DIM*DIM

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

// Is MCNK (chunkRow, chunkCol) of tile (x,y) holed in the horizon mesh?
// Row/col outside [0,16), absent tiles, and an unallocated hole mask all read
// as "not a hole".
bool wdlChunkIsHole(const Wdl& wdl, int x, int y, int chunkRow, int chunkCol);

// Set/clear one MAHO hole bit. Out-of-range tile or row/col is ignored.
// Allocates the hole mask on first use so hand-built Wdls stay cheap.
void setWdlHole(Wdl& wdl, int x, int y, int chunkRow, int chunkCol, bool hole);

// Patch the hole masks back into a .wdl's bytes. `wdl` must come from
// parseWdl(wdlBuf) (same buffer, so its mahoOffset values index into it); each
// present tile's 16 uint16 rows are written little-endian over the original
// MAHO payload. Returns a new buffer that is byte-identical to `wdlBuf` except
// for the patched masks -- the surgical, verifiable inverse of the hole read
// path. Tiles with mahoOffset == 0 (absent, or a pre-WotLK file without the
// chunk -- we patch in place, we do not grow files) are skipped. Throws if an
// offset + 32 bytes runs past the buffer (a sign `wdl` and `wdlBuf` don't
// match).
std::vector<uint8_t> writeWdlHoles(const std::vector<uint8_t>& wdlBuf, const Wdl& wdl);

// Blizzard's ADT->WDL sync rule: a WDL hole bit is set exactly when the whole
// MCNK is holed, i.e. ALL 16 low-res ADT hole bits are set.
inline bool adtChunkFullyHoled(uint16_t adtHoles) { return adtHoles == 0xFFFF; }

// Re-derive one WDL hole bit from a chunk's ADT hole mask (the editor's hole
// tool calls this after every hole edit): set iff fully holed, clear otherwise.
void syncWdlFromAdt(Wdl& wdl, int x, int y, int chunkRow, int chunkCol, uint16_t adtHoles);

}  // namespace wf
