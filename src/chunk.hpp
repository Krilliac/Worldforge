#pragma once
// ---------------------------------------------------------------------------
// Chunk iteration for WoW's IFF-style container files (WDT, ADT, WMO, M2...).
//
// Layout: [4-byte magic (reversed on disk)] [uint32 size] [size bytes payload].
// Vanilla 1.12.1 ADTs are MONOLITHIC (one .adt file); the split _obj0/_tex0
// variants are Cataclysm+. So a single linear scan reaches every chunk we need:
// MMDX/MMID/MWMO/MWID/MDDF/MODF all live in the one file.
// ---------------------------------------------------------------------------
#include "byte_reader.hpp"
#include <functional>
#include <string>

namespace wf {

struct Chunk {
    std::string    magic; // human-readable, e.g. "MDDF"
    const uint8_t* data;  // pointer to payload (not owned)
    uint32_t       size;  // payload size in bytes
};

// Walk top-level chunks. Return false from `cb` to stop early.
// Throws std::runtime_error on a chunk whose declared size overruns the buffer.
inline void forEachChunk(const uint8_t* buf, size_t len,
                         const std::function<bool(const Chunk&)>& cb) {
    ByteReader r(buf, len);
    while (r.remaining() >= 8) {
        std::string magic = r.fourccReversed();
        uint32_t    size  = r.u32();
        if (r.remaining() < size) {
            throw std::runtime_error("Chunk '" + magic + "' declares size " +
                                     std::to_string(size) + " but only " +
                                     std::to_string(r.remaining()) + " bytes remain");
        }
        Chunk c{ magic, r.ptr(), size };
        if (!cb(c)) return;
        r.skip(size);
    }
}

} // namespace wf
