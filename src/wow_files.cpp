#include "wow_files.hpp"
#include "byte_reader.hpp"
#include "chunk.hpp"

#include <cstring>
#include <stdexcept>

namespace wf {

// ===========================================================================
// WDT
// ===========================================================================
Wdt parseWdt(const std::vector<uint8_t>& buf) {
    Wdt wdt;
    bool sawMain = false;

    forEachChunk(buf.data(), buf.size(), [&](const Chunk& c) {
        if (c.magic == "MPHD") {
            // SMMapHeader: first uint32 is the flags field.
            if (c.size >= 4) {
                ByteReader r(c.data, c.size);
                wdt.mphdFlags = r.u32();
                wdt.globalWmo = (wdt.mphdFlags & Wdt::MPHD_GLOBAL_WMO) != 0;
            }
        } else if (c.magic == "MAIN") {
            // 64*64 entries of 8 bytes: { uint32 flags; uint32 asyncId }.
            // flags & 0x1 == this tile has an ADT file.
            if (c.size < 64u * 64u * 8u) {
                throw std::runtime_error("MAIN chunk too small: " +
                                         std::to_string(c.size) + " bytes");
            }
            ByteReader r(c.data, c.size);
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    uint32_t flags = r.u32();
                    r.u32(); // asyncId / unused
                    wdt.tiles[static_cast<size_t>(y) * 64 + x] = (flags & 0x1) != 0;
                }
            }
            sawMain = true;
        }
        return true; // keep scanning
    });

    if (!sawMain) throw std::runtime_error("WDT has no MAIN chunk");
    return wdt;
}

// ===========================================================================
// ADT helpers
// ===========================================================================
namespace {

// Split a NUL-separated name blob into (offset -> string). We keep the raw blob
// and resolve by offset, but this also lets us sanity-check.
std::string stringAtOffset(const std::vector<char>& blob, uint32_t offset) {
    if (offset >= blob.size()) return "<bad-offset:" + std::to_string(offset) + ">";
    const char* start = blob.data() + offset;
    size_t maxLen = blob.size() - offset;
    size_t len = ::strnlen(start, maxLen);
    return std::string(start, len);
}

} // namespace

Adt parseAdt(const std::vector<uint8_t>& buf) {
    Adt adt;

    forEachChunk(buf.data(), buf.size(), [&](const Chunk& c) {
        if (c.magic == "MTEX") {
            const char* p = reinterpret_cast<const char*>(c.data);
            size_t i = 0;
            while (i < c.size) {
                size_t len = ::strnlen(p + i, c.size - i);
                if (len > 0) adt.textures.emplace_back(p + i, len);
                i += len + 1;
            }
        } else if (c.magic == "MMDX") {
            adt.m2NameBlob.assign(reinterpret_cast<const char*>(c.data),
                                  reinterpret_cast<const char*>(c.data) + c.size);
        } else if (c.magic == "MMID") {
            ByteReader r(c.data, c.size);
            adt.m2Offsets.reserve(c.size / 4);
            while (r.remaining() >= 4) adt.m2Offsets.push_back(r.u32());
        } else if (c.magic == "MWMO") {
            adt.wmoNameBlob.assign(reinterpret_cast<const char*>(c.data),
                                   reinterpret_cast<const char*>(c.data) + c.size);
        } else if (c.magic == "MWID") {
            ByteReader r(c.data, c.size);
            adt.wmoOffsets.reserve(c.size / 4);
            while (r.remaining() >= 4) adt.wmoOffsets.push_back(r.u32());
        } else if (c.magic == "MDDF") {
            ByteReader r(c.data, c.size);
            adt.doodads.reserve(c.size / 36);
            while (r.remaining() >= 36) {
                DoodadDef d;
                d.mmidIndex = r.u32();
                d.uniqueId  = r.u32();
                for (float& v : d.pos) v = r.f32();
                for (float& v : d.rot) v = r.f32();
                d.scale = r.u16();
                d.flags = r.u16();
                adt.doodads.push_back(d);
            }
        } else if (c.magic == "MODF") {
            ByteReader r(c.data, c.size);
            adt.wmos.reserve(c.size / 64);
            while (r.remaining() >= 64) {
                WmoDef w;
                w.mwidIndex = r.u32();
                w.uniqueId  = r.u32();
                for (float& v : w.pos)     v = r.f32();
                for (float& v : w.rot)     v = r.f32();
                for (float& v : w.extents) v = r.f32();
                w.flags     = r.u16();
                w.doodadSet = r.u16();
                w.nameSet   = r.u16();
                r.u16(); // padding
                adt.wmos.push_back(w);
            }
        }
        return true;
    });

    // Resolve model names: placement index -> MMID/MWID offset -> name blob.
    for (DoodadDef& d : adt.doodads) {
        if (d.mmidIndex < adt.m2Offsets.size())
            d.modelName = stringAtOffset(adt.m2NameBlob, adt.m2Offsets[d.mmidIndex]);
        else
            d.modelName = "<mmid-oob:" + std::to_string(d.mmidIndex) + ">";
    }
    for (WmoDef& w : adt.wmos) {
        if (w.mwidIndex < adt.wmoOffsets.size())
            w.modelName = stringAtOffset(adt.wmoNameBlob, adt.wmoOffsets[w.mwidIndex]);
        else
            w.modelName = "<mwid-oob:" + std::to_string(w.mwidIndex) + ">";
    }

    return adt;
}

// ===========================================================================
// DBC
// ===========================================================================
Dbc Dbc::parse(const std::vector<uint8_t>& buf) {
    Dbc dbc;
    ByteReader r(buf);

    std::string magic = r.fourccRaw(); // DBC magic is stored forward: "WDBC"
    if (magic != "WDBC")
        throw std::runtime_error("Not a DBC file (magic='" + magic + "')");

    dbc.recordCount_      = r.u32();
    dbc.fieldCount_       = r.u32();
    dbc.recordSize_       = r.u32();
    uint32_t stringSize   = r.u32();

    const uint64_t recBytes = static_cast<uint64_t>(dbc.recordCount_) * dbc.recordSize_;
    if (r.remaining() < recBytes + stringSize)
        throw std::runtime_error("DBC truncated: header promises more data than present");

    dbc.records_.assign(r.ptr(), r.ptr() + recBytes);
    r.skip(static_cast<size_t>(recBytes));
    dbc.strings_.assign(reinterpret_cast<const char*>(r.ptr()),
                        reinterpret_cast<const char*>(r.ptr()) + stringSize);
    return dbc;
}

uint32_t Dbc::getU32(uint32_t rec, uint32_t field) const {
    if (rec >= recordCount_ || field >= fieldCount_)
        throw std::out_of_range("DBC::getU32 index out of range");
    const size_t off = static_cast<size_t>(rec) * recordSize_ + static_cast<size_t>(field) * 4;
    ByteReader r(records_.data() + off, 4);
    return r.u32();
}

std::string Dbc::getString(uint32_t rec, uint32_t field) const {
    uint32_t offset = getU32(rec, field);
    if (offset == 0 || offset >= strings_.size()) return std::string();
    const char* start = strings_.data() + offset;
    size_t maxLen = strings_.size() - offset;
    return std::string(start, ::strnlen(start, maxLen));
}

} // namespace wf
