#pragma once
// ---------------------------------------------------------------------------
// Parsers for the file formats P0 needs: WDT (tile index), ADT (placements),
// and DBC (generic record reader). Struct layouts verified against wowdev.wiki
// ADT/v18 + WDT and cross-checked with cmangos/mangos extractor sources.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wf {

// ===========================================================================
// WDT  --  which of the 64x64 ADT tiles actually exist for a map.
// ===========================================================================
struct Wdt {
    uint32_t mphdFlags = 0;
    bool     globalWmo = false;          // mphdFlags & 0x1  (WMO-only map, e.g. a dungeon)
    std::array<bool, 64 * 64> tiles{};   // index = y * 64 + x

    bool hasTile(int x, int y) const {
        if (x < 0 || x >= 64 || y < 0 || y >= 64) return false;
        return tiles[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)];
    }
};

// Parse a WDT buffer. Throws std::runtime_error on malformed input.
Wdt parseWdt(const std::vector<uint8_t>& buf);

// ===========================================================================
// ADT  --  static placements (doodads = M2, map objects = WMO).
// ===========================================================================

// MDDF entry: 36 bytes. wowdev.wiki ADT/v18 #MDDF.
struct DoodadDef {
    uint32_t    mmidIndex = 0;   // -> MMID -> byte offset into MMDX name blob
    uint32_t    uniqueId  = 0;
    float       pos[3]    = {0, 0, 0};
    float       rot[3]    = {0, 0, 0};   // degrees
    uint16_t    scale     = 1024;        // 1024 == 1.0
    uint16_t    flags     = 0;
    std::string modelName;               // resolved from MMDX/MMID
};

// MODF entry: 64 bytes. wowdev.wiki ADT/v18 #MODF.
struct WmoDef {
    uint32_t    mwidIndex = 0;   // -> MWID -> byte offset into MWMO name blob
    uint32_t    uniqueId  = 0;
    float       pos[3]    = {0, 0, 0};
    float       rot[3]    = {0, 0, 0};
    // Two C3Vectors of extents. NOTE: sources disagree on min/max ORDER
    // (wowdev WDT page: upper-then-lower; cmangos/mangos: lower-then-upper).
    // Irrelevant for a placement dump, but DO NOT assume an order before any
    // collision/culling use -- resolve against real data first.
    float       extents[6] = {0, 0, 0, 0, 0, 0};
    uint16_t    flags     = 0;
    uint16_t    doodadSet = 0;
    uint16_t    nameSet   = 0;
    std::string modelName;               // resolved from MWMO/MWID
};

struct Adt {
    // Raw NUL-separated name blobs + offset tables, kept so model names can be
    // resolved exactly the way the client does (index -> offset -> string).
    std::vector<char>     m2NameBlob;    // MMDX
    std::vector<uint32_t> m2Offsets;     // MMID (offsets into m2NameBlob)
    std::vector<char>     wmoNameBlob;   // MWMO
    std::vector<uint32_t> wmoOffsets;    // MWID (offsets into wmoNameBlob)

    std::vector<DoodadDef> doodads;      // MDDF
    std::vector<WmoDef>    wmos;         // MODF
};

// Parse an ADT buffer and resolve all model names. Throws on malformed input.
Adt parseAdt(const std::vector<uint8_t>& buf);

// ===========================================================================
// DBC  --  generic record store. Header is "WDBC" + 4 uint32 (rec count,
// field count, record size, string-block size). Field SCHEMA is per-table and
// intentionally NOT hardcoded here (column meanings vary and are a verification
// hazard); callers pull fields by index once they've confirmed the layout.
// ===========================================================================
class Dbc {
public:
    // Parse a DBC buffer. Throws std::runtime_error if the magic/layout is wrong.
    static Dbc parse(const std::vector<uint8_t>& buf);

    uint32_t recordCount() const noexcept { return recordCount_; }
    uint32_t fieldCount()  const noexcept { return fieldCount_; }
    uint32_t recordSize()  const noexcept { return recordSize_; }

    // Read field `field` (0-based) of record `rec` as a uint32.
    uint32_t getU32(uint32_t rec, uint32_t field) const;

    // Read a string field: the field holds a byte offset into the string block.
    // Returns "" for a zero/out-of-range offset.
    std::string getString(uint32_t rec, uint32_t field) const;

private:
    uint32_t              recordCount_ = 0;
    uint32_t              fieldCount_  = 0;
    uint32_t              recordSize_  = 0;
    std::vector<uint8_t>  records_;
    std::vector<char>     strings_;
};

} // namespace wf
