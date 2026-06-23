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

#include "wow_files.hpp"   // Dbc

namespace wf {

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
    std::array<uint32_t, 8> lightParams{}; // fields 7..14 (LightParams.dbc refs)
};

// Typed accessors. `rec` is a 0-based record index in `[0, dbc.recordCount())`.
MapEntry        mapEntry(const Dbc& dbc, uint32_t rec);
AreaEntry       areaEntry(const Dbc& dbc, uint32_t rec);
LiquidTypeEntry liquidTypeEntry(const Dbc& dbc, uint32_t rec);
LightEntry      lightEntry(const Dbc& dbc, uint32_t rec);

} // namespace wf
