#pragma once
// ---------------------------------------------------------------------------
// Typed views over the generic Dbc reader (wow_files.hpp) for the DBC tables the
// editor needs to resolve ids <-> names/params: Map, AreaTable, LiquidType,
// Light. Field indices are the verified vanilla 1.12.1 (build 5875) layouts
// (cross-checked vs mangos-zero DBCStructure.h/DBCfmt.h and wowdev.wiki). Every
// DBC field is 4 bytes; string fields hold a byte offset into the string block;
// the locale string arrays are 8 slots + 1 flags, enUS = slot 0.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "wow_files.hpp"   // Dbc

namespace wf {

// Builds a WDBC blob (the inverse of Dbc::parse) for editing DBCs / custom
// content -- e.g. tweak a record and repackage via writeMpqArchive. Records are
// fixed-width rows of `fieldCount` uint32s; string fields hold an offset
// returned by addString. Dedups strings; offset 0 is always the empty string.
class DbcBuilder {
public:
    explicit DbcBuilder(uint32_t fieldCount) : fieldCount_(fieldCount) {
        strings_.push_back('\0');     // offset 0 == ""
    }
    uint32_t addString(const std::string& s);
    void addRecord(const std::vector<uint32_t>& fields);  // padded/truncated to fieldCount
    std::vector<uint8_t> build() const;

private:
    uint32_t fieldCount_;
    std::vector<std::vector<uint32_t>> records_;
    std::string strings_;
    std::unordered_map<std::string, uint32_t> stringOffsets_;
};

struct MapEntry {
    uint32_t    id           = 0;   // field 0
    std::string directory;          // field 1 (string) -- e.g. "Azeroth"
    uint32_t    instanceType = 0;   // field 2 -- 0 world,1 instance,2 raid,3 BG
    std::string name;               // field 4 (string, enUS)
};

struct AreaEntry {
    uint32_t    id               = 0;   // field 0
    uint32_t    mapId            = 0;   // field 1 (ContinentID)
    uint32_t    parentAreaId     = 0;   // field 2
    uint32_t    areaBit          = 0;   // field 3 (explore flag)
    uint32_t    flags            = 0;   // field 4
    int32_t     explorationLevel = 0;   // field 10
    std::string name;                   // field 11 (string, enUS)
};

struct LiquidTypeEntry {
    uint32_t id       = 0;   // field 0
    uint32_t liquidId = 0;   // field 1 (visual ref: 23 water,29 ocean,35 magma,41 slime)
    uint32_t type     = 0;   // field 2 (0 magma,2 slime,3 water)
    uint32_t spellId  = 0;   // field 3
};

struct LightEntry {
    uint32_t id           = 0;   // field 0
    uint32_t mapId        = 0;   // field 1
    float    x = 0, y = 0, z = 0;          // fields 2,3,4 (game coords)
    float    falloffStart = 0;   // field 5
    float    falloffEnd   = 0;   // field 6
    // fields 7..11 -- 5 LightParams.dbc refs (clear-weather, fog, rain, ...).
    // Vanilla 1.12 Light.dbc has exactly 12 fields, so there are 5 params, NOT 8;
    // reading 8 walks past the record and the string block (verified vs real dbc).
    std::array<uint32_t, 5> lightParams{};
};

struct CreatureModelDataEntry {
    uint32_t    id        = 0;   // field 0
    std::string modelPath;       // field 2 (string) e.g. "Creature\\Rabbit\\Rabbit.mdx"
};

struct CreatureDisplayInfoEntry {
    uint32_t id      = 0;        // field 0 (Displayid)
    uint32_t modelId = 0;        // field 1 -> CreatureModelData.id
    float    scale   = 1.0f;     // field 4 (CreatureModelScale) -- VERIFY-FLAGGED
};

struct GameObjectDisplayInfoEntry {
    uint32_t    id = 0;          // field 0 (Displayid)
    std::string modelName;       // field 1 (string) -- .mdx (M2) or .wmo path
};

struct GroundEffectDoodadEntry {
    uint32_t    id = 0;          // field 0
    std::string modelPath;       // field 1 (string) -- doodad .mdx/.m2
};

struct GroundEffectTextureEntry {
    uint32_t id = 0;                    // field 0
    std::array<uint32_t, 4> doodadIds{};// fields 1..4 -> GroundEffectDoodad ids
    uint32_t density = 0;               // field 9 (VERIFY-FLAGGED)
};

// Typed accessors. `rec` is a 0-based record index in `[0, dbc.recordCount())`.
MapEntry        mapEntry(const Dbc& dbc, uint32_t rec);
AreaEntry       areaEntry(const Dbc& dbc, uint32_t rec);
LiquidTypeEntry liquidTypeEntry(const Dbc& dbc, uint32_t rec);
LightEntry      lightEntry(const Dbc& dbc, uint32_t rec);
CreatureModelDataEntry     creatureModelDataEntry(const Dbc& dbc, uint32_t rec);
CreatureDisplayInfoEntry   creatureDisplayInfoEntry(const Dbc& dbc, uint32_t rec);
GameObjectDisplayInfoEntry gameObjectDisplayInfoEntry(const Dbc& dbc, uint32_t rec);
GroundEffectDoodadEntry    groundEffectDoodadEntry(const Dbc& dbc, uint32_t rec);
GroundEffectTextureEntry   groundEffectTextureEntry(const Dbc& dbc, uint32_t rec);

// Normalise a DBC model path to the on-disk file: vanilla DBCs reference models
// with a .mdx (or .mdl) extension but the archive stores .m2 (MD20). Replaces a
// trailing .mdx/.mdl with .m2 (case-insensitive); leaves .wmo and others as-is.
std::string normalizeModelPath(const std::string& dbcPath);

} // namespace wf
