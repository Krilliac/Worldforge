#include "wdl.hpp"

#include "byte_reader.hpp"
#include "chunk.hpp"

#include <stdexcept>

namespace wf {

namespace {
constexpr int    kInnerHeights  = 16 * 16;                    // MARE inner grid
constexpr size_t kMahoBytes     = Wdl::HOLE_ROWS * 2u;        // 32-byte payload

bool holeIndexOk(int x, int y, int chunkRow, int chunkCol) {
    return x >= 0 && x < Wdl::DIM && y >= 0 && y < Wdl::DIM &&
           chunkRow >= 0 && chunkRow < Wdl::HOLE_ROWS &&
           chunkCol >= 0 && chunkCol < Wdl::HOLE_ROWS;
}
}  // namespace

Wdl parseWdl(const std::vector<uint8_t>& buf) {
    Wdl w;
    w.outer.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM * Wdl::N, 0);
    w.holes.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM * Wdl::HOLE_ROWS, 0);
    w.mahoOffset.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM, 0);

    // Locate MAOF (the table of per-tile file offsets).
    const uint8_t* maof = nullptr; uint32_t maofSize = 0;
    forEachChunk(buf.data(), buf.size(), [&](const Chunk& c) {
        if (c.magic == "MAOF") { maof = c.data; maofSize = c.size; return false; }
        return true;
    });
    if (!maof || maofSize < static_cast<uint32_t>(Wdl::DIM * Wdl::DIM) * 4u) return w;

    ByteReader off(maof, maofSize);
    for (int t = 0; t < Wdl::DIM * Wdl::DIM; ++t) {
        const uint32_t o = off.u32();
        if (o == 0 || static_cast<size_t>(o) + 8 > buf.size()) continue;

        // MAOF points at the MARE chunk start (magic+size); a few writers point at
        // the data directly. Detect via the magic and skip the 8-byte header if present.
        size_t dataOfs = o;
        {
            ByteReader probe(buf.data() + o, buf.size() - o);
            if (probe.fourccReversed() == "MARE") { probe.u32(); dataOfs = o + 8; }
        }
        if (dataOfs + 2 > buf.size()) continue;

        ByteReader r(buf.data() + dataOfs, buf.size() - dataOfs);
        const size_t base = static_cast<size_t>(t) * Wdl::N;
        int i = 0;
        for (; i < Wdl::N && r.remaining() >= 2; ++i)
            w.outer[base + i] = static_cast<int16_t>(r.u16());
        if (i != Wdl::N) continue;              // only a full outer grid counts
        w.present[t] = true;

        // MAHO (the hole mask) trails MARE's 16x16 inner grid. Missing chunk
        // (pre-WotLK writers) -> all zeros and mahoOffset stays 0.
        if (r.remaining() < static_cast<size_t>(kInnerHeights) * 2u + 8u + kMahoBytes)
            continue;
        r.skip(static_cast<size_t>(kInnerHeights) * 2u);   // inner heights, unused
        if (r.fourccReversed() != "MAHO") continue;
        if (r.u32() < kMahoBytes) continue;                // truncated payload
        w.mahoOffset[t] = static_cast<uint32_t>(dataOfs + r.pos());
        const size_t hbase = static_cast<size_t>(t) * Wdl::HOLE_ROWS;
        for (int row = 0; row < Wdl::HOLE_ROWS; ++row)
            w.holes[hbase + row] = r.u16();
    }
    return w;
}

bool wdlChunkIsHole(const Wdl& wdl, int x, int y, int chunkRow, int chunkCol) {
    if (wdl.holes.empty()) return false;        // hand-built Wdl, mask never touched
    if (!holeIndexOk(x, y, chunkRow, chunkCol)) return false;
    const size_t row = (static_cast<size_t>(y) * Wdl::DIM + x) * Wdl::HOLE_ROWS + chunkRow;
    return ((wdl.holes[row] >> chunkCol) & 1u) != 0;
}

void setWdlHole(Wdl& wdl, int x, int y, int chunkRow, int chunkCol, bool hole) {
    if (!holeIndexOk(x, y, chunkRow, chunkCol)) return;        // out of range: ignore
    if (wdl.holes.empty())
        wdl.holes.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM * Wdl::HOLE_ROWS, 0);
    const size_t row = (static_cast<size_t>(y) * Wdl::DIM + x) * Wdl::HOLE_ROWS + chunkRow;
    if (hole) wdl.holes[row] |= static_cast<uint16_t>(1u << chunkCol);
    else      wdl.holes[row] &= static_cast<uint16_t>(~(1u << chunkCol));
}

std::vector<uint8_t> writeWdlHoles(const std::vector<uint8_t>& wdlBuf, const Wdl& wdl) {
    std::vector<uint8_t> out = wdlBuf;   // start byte-identical to the original
    if (wdl.holes.empty() || wdl.mahoOffset.empty()) return out;   // nothing to patch
    for (int t = 0; t < Wdl::DIM * Wdl::DIM; ++t) {
        const size_t base = wdl.mahoOffset[t];
        if (base == 0) continue;                 // no in-file MAHO to patch
        if (base + kMahoBytes > out.size())
            throw std::runtime_error("writeWdlHoles: MAHO offset past end of WDL "
                                     "(wdl does not match this buffer)");
        const size_t hbase = static_cast<size_t>(t) * Wdl::HOLE_ROWS;
        for (int row = 0; row < Wdl::HOLE_ROWS; ++row) {
            const uint16_t v = wdl.holes[hbase + row];
            out[base + static_cast<size_t>(row) * 2 + 0] = static_cast<uint8_t>(v & 0xFF);
            out[base + static_cast<size_t>(row) * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        }
    }
    return out;
}

void syncWdlFromAdt(Wdl& wdl, int x, int y, int chunkRow, int chunkCol, uint16_t adtHoles) {
    setWdlHole(wdl, x, y, chunkRow, chunkCol, adtChunkFullyHoled(adtHoles));
}

}  // namespace wf
