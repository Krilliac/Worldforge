#include "wdl.hpp"

#include "byte_reader.hpp"
#include "chunk.hpp"

namespace wf {

Wdl parseWdl(const std::vector<uint8_t>& buf) {
    Wdl w;
    w.outer.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM * Wdl::N, 0);

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
        if (i == Wdl::N) w.present[t] = true;   // only a full outer grid counts
    }
    return w;
}

}  // namespace wf
