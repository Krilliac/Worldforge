#include "dbc_defs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace wf {

namespace {
// A DBC float field is just a uint32 reinterpreted.
float fieldF32(const Dbc& dbc, uint32_t rec, uint32_t field) {
    uint32_t bits = dbc.getU32(rec, field);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// Case-insensitive test for a trailing extension (suffix includes the dot).
bool endsWithCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::tolower((unsigned char)s[off + i]));
        char b = static_cast<char>(std::tolower((unsigned char)suffix[i]));
        if (a != b) return false;
    }
    return true;
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
} // namespace

uint32_t DbcBuilder::addString(const std::string& s) {
    if (s.empty()) return 0;
    auto it = stringOffsets_.find(s);
    if (it != stringOffsets_.end()) return it->second;
    uint32_t off = static_cast<uint32_t>(strings_.size());
    stringOffsets_.emplace(s, off);
    strings_ += s;
    strings_.push_back('\0');
    return off;
}

void DbcBuilder::addRecord(const std::vector<uint32_t>& fields) {
    std::vector<uint32_t> row(fieldCount_, 0);
    for (uint32_t i = 0; i < fieldCount_ && i < fields.size(); ++i) row[i] = fields[i];
    records_.push_back(std::move(row));
}

std::vector<uint8_t> DbcBuilder::build() const {
    std::vector<uint8_t> b;
    b.push_back('W'); b.push_back('D'); b.push_back('B'); b.push_back('C');
    put32(b, static_cast<uint32_t>(records_.size()));
    put32(b, fieldCount_);
    put32(b, fieldCount_ * 4);                          // recordSize
    put32(b, static_cast<uint32_t>(strings_.size()));   // stringSize
    for (const auto& row : records_)
        for (uint32_t v : row) put32(b, v);
    b.insert(b.end(), strings_.begin(), strings_.end());
    return b;
}

// ===========================================================================
// Column schemas (see the header). Aggregate order is
//   { name, type, isId, fkTable, fkColumn, arrayLen, verified }.
// Full-record schemas carry a static_assert pinning schemaFieldCount to the
// known 1.12.1.5875 record stride / 4; prefix schemas (Map, the display-info
// tables, GroundEffectDoodad) cover only the verified leading columns.
// Column-offset static_asserts cross-check the schemas against the hardcoded
// field indices the typed readers below use.
// ===========================================================================
namespace {

using CT = ColType;

constexpr ColumnDef kMapCols[] = {
    {"ID", CT::U32, true},
    {"Directory", CT::Str},
    {"InstanceType", CT::U32},
    {"PVP", CT::U32, false, nullptr, nullptr, 1, false},
    {"MapName_lang", CT::LocStr},
};  // verified prefix (vanilla Map.dbc carries many more trailing fields)

constexpr ColumnDef kAreaTableCols[] = {
    {"ID", CT::U32, true},
    {"ContinentID", CT::U32, false, "Map", "ID"},
    {"ParentAreaNum", CT::U32, false, "AreaTable", "ID"},
    {"AreaBit", CT::U32},
    {"Flags", CT::U32},
    {"SoundProviderPref", CT::U32},
    {"SoundProviderPrefUnderwater", CT::U32},
    {"AmbienceID", CT::U32},
    {"ZoneMusic", CT::U32},
    {"IntroSound", CT::U32},
    {"ExplorationLevel", CT::I32},
    {"AreaName_lang", CT::LocStr},
    {"FactionGroupMask", CT::U32},
    {"LiquidTypeID", CT::U32, false, "LiquidType", "ID", 4},
    {"MinElevation", CT::F32},
    {"Ambient_multiplier", CT::F32},
    {"Lightid", CT::U32, false, "Light", "ID"},
};

constexpr ColumnDef kLiquidTypeCols[] = {
    {"ID", CT::U32, true},
    {"Name", CT::Str},
    {"Type", CT::U32},
    {"SpellID", CT::U32, false, "Spell", "ID"},
};

constexpr ColumnDef kCreatureDisplayInfoCols[] = {
    {"ID", CT::U32, true},
    {"ModelID", CT::U32, false, "CreatureModelData", "ID"},
    {"SoundID", CT::U32, false, nullptr, nullptr, 1, false},
    {"ExtendedDisplayInfoID", CT::U32, false, nullptr, nullptr, 1, false},
    {"CreatureModelScale", CT::F32, false, nullptr, nullptr, 1, false},  // VERIFY-FLAGGED
};  // verified prefix

constexpr ColumnDef kCreatureModelDataCols[] = {
    {"ID", CT::U32, true},
    {"Flags", CT::U32, false, nullptr, nullptr, 1, false},
    {"ModelName", CT::Str},
};  // verified prefix

constexpr ColumnDef kGameObjectDisplayInfoCols[] = {
    {"ID", CT::U32, true},
    {"ModelName", CT::Str},
};  // verified prefix

constexpr ColumnDef kItemDisplayInfoCols[] = {
    {"ID", CT::U32, true},
    {"ModelName", CT::Str, false, nullptr, nullptr, 2},
    {"ModelTexture", CT::Str, false, nullptr, nullptr, 2},
    {"InventoryIcon", CT::Str, false, nullptr, nullptr, 2},
    {"GeosetGroup", CT::U32, false, nullptr, nullptr, 3},
    {"Flags", CT::U32},
    {"SpellVisualID", CT::U32},
    {"GroupSoundIndex", CT::U32},
    {"HelmetGeosetVis", CT::U32, false, nullptr, nullptr, 2},
    {"Texture", CT::Str, false, nullptr, nullptr, 8},
};

constexpr ColumnDef kAnimationDataCols[] = {
    {"ID", CT::U32, true},
    {"Name", CT::Str},
    {"Weaponflags", CT::U32},
    {"Bodyflags", CT::U32},
    {"Flags", CT::U32},
    {"Fallback", CT::U32, false, "AnimationData", "ID"},
    {"BehaviorID", CT::U32, false, "AnimationData", "ID"},
};

constexpr ColumnDef kGroundEffectTextureCols[] = {
    {"ID", CT::U32, true},
    {"DoodadID", CT::U32, false, "GroundEffectDoodad", "ID", 4},
    {"DoodadWeight", CT::U32, false, nullptr, nullptr, 4, false},
    {"Density", CT::U32, false, nullptr, nullptr, 1, false},  // VERIFY-FLAGGED
    {"Sound", CT::U32, false, nullptr, nullptr, 1, false},
};

constexpr ColumnDef kGroundEffectDoodadCols[] = {
    {"ID", CT::U32, true},
    {"Doodadpath", CT::Str},
};  // verified prefix

constexpr ColumnDef kLightCols[] = {
    {"ID", CT::U32, true},
    {"ContinentID", CT::U32, false, "Map", "ID"},
    {"X", CT::F32},
    {"Y", CT::F32},
    {"Z", CT::F32},
    {"FalloffStart", CT::F32},
    {"FalloffEnd", CT::F32},
    {"LightParamsID", CT::U32, false, "LightParams", "ID", 5},
};

constexpr ColumnDef kLightParamsCols[] = {
    {"ID", CT::U32, true},
    {"HighlightSky", CT::U32},
    {"LightSkyboxID", CT::U32, false, "LightSkybox", "ID"},
    {"Glow", CT::F32},
    {"WaterShallowAlpha", CT::F32},
    {"WaterDeepAlpha", CT::F32},
    {"OceanShallowAlpha", CT::F32},
    {"OceanDeepAlpha", CT::F32},
};

constexpr ColumnDef kLightIntBandCols[] = {
    {"ID", CT::U32, true},
    {"Num", CT::U32},
    {"Time", CT::U32, false, nullptr, nullptr, 16},
    {"Color", CT::U32, false, nullptr, nullptr, 16},
};

constexpr ColumnDef kLightFloatBandCols[] = {
    {"ID", CT::U32, true},
    {"Num", CT::U32},
    {"Time", CT::U32, false, nullptr, nullptr, 16},
    {"Data", CT::F32, false, nullptr, nullptr, 16},
};

constexpr ColumnDef kLightSkyboxCols[] = {
    {"ID", CT::U32, true},
    {"Name", CT::Str},
    {"Flags", CT::U32, false, nullptr, nullptr, 1, false},  // TBC+; vanilla reads 0
};

constexpr TableSchema kSchemas[] = {
    {"Map",                   kMapCols,                   sizeof(kMapCols) / sizeof(kMapCols[0])},
    {"AreaTable",             kAreaTableCols,             sizeof(kAreaTableCols) / sizeof(kAreaTableCols[0])},
    {"LiquidType",            kLiquidTypeCols,            sizeof(kLiquidTypeCols) / sizeof(kLiquidTypeCols[0])},
    {"CreatureDisplayInfo",   kCreatureDisplayInfoCols,   sizeof(kCreatureDisplayInfoCols) / sizeof(kCreatureDisplayInfoCols[0])},
    {"CreatureModelData",     kCreatureModelDataCols,     sizeof(kCreatureModelDataCols) / sizeof(kCreatureModelDataCols[0])},
    {"GameObjectDisplayInfo", kGameObjectDisplayInfoCols, sizeof(kGameObjectDisplayInfoCols) / sizeof(kGameObjectDisplayInfoCols[0])},
    {"ItemDisplayInfo",       kItemDisplayInfoCols,       sizeof(kItemDisplayInfoCols) / sizeof(kItemDisplayInfoCols[0])},
    {"AnimationData",         kAnimationDataCols,         sizeof(kAnimationDataCols) / sizeof(kAnimationDataCols[0])},
    {"GroundEffectTexture",   kGroundEffectTextureCols,   sizeof(kGroundEffectTextureCols) / sizeof(kGroundEffectTextureCols[0])},
    {"GroundEffectDoodad",    kGroundEffectDoodadCols,    sizeof(kGroundEffectDoodadCols) / sizeof(kGroundEffectDoodadCols[0])},
    {"Light",                 kLightCols,                 sizeof(kLightCols) / sizeof(kLightCols[0])},
    {"LightParams",           kLightParamsCols,           sizeof(kLightParamsCols) / sizeof(kLightParamsCols[0])},
    {"LightIntBand",          kLightIntBandCols,          sizeof(kLightIntBandCols) / sizeof(kLightIntBandCols[0])},
    {"LightFloatBand",        kLightFloatBandCols,        sizeof(kLightFloatBandCols) / sizeof(kLightFloatBandCols[0])},
    {"LightSkybox",           kLightSkyboxCols,           sizeof(kLightSkyboxCols) / sizeof(kLightSkyboxCols[0])},
};
constexpr size_t kSchemaCount = sizeof(kSchemas) / sizeof(kSchemas[0]);

// Full-record field counts vs the known 1.12.1.5875 record strides (stride/4).
static_assert(schemaFieldCount(kSchemas[1])  == 28, "AreaTable is 28 fields (112 bytes)");
static_assert(schemaFieldCount(kSchemas[2])  == 4,  "LiquidType is 4 fields (16 bytes)");
static_assert(schemaFieldCount(kSchemas[6])  == 23, "ItemDisplayInfo is 23 fields (92 bytes)");
static_assert(schemaFieldCount(kSchemas[7])  == 7,  "AnimationData is 7 fields (28 bytes)");
static_assert(schemaFieldCount(kSchemas[8])  == 11, "GroundEffectTexture is 11 fields (44 bytes)");
static_assert(schemaFieldCount(kSchemas[10]) == 12, "Light is 12 fields (48 bytes)");
static_assert(schemaFieldCount(kSchemas[11]) == 8,  "LightParams is 8 fields (32 bytes)");
static_assert(schemaFieldCount(kSchemas[12]) == 34, "LightIntBand is 34 fields (136 bytes)");
static_assert(schemaFieldCount(kSchemas[13]) == 34, "LightFloatBand is 34 fields (136 bytes)");

// Pure-4-byte-field structs mirror their record byte-for-byte.
static_assert(sizeof(LightEntry)          == 12 * 4, "LightEntry mirrors the 12-field record");
static_assert(sizeof(LightParamsRecord)   == 8 * 4,  "LightParamsRecord mirrors the 8-field record");
static_assert(sizeof(LightIntBandEntry)   == 34 * 4, "LightIntBandEntry mirrors the 34-field record");
static_assert(sizeof(LightFloatBandEntry) == 34 * 4, "LightFloatBandEntry mirrors the 34-field record");

// Schema column offsets match the typed readers' hardcoded field indices.
static_assert(schemaFieldOffsetOf(kSchemas[1], "ExplorationLevel") == 10);
static_assert(schemaFieldOffsetOf(kSchemas[1], "AreaName_lang")    == 11);
static_assert(schemaFieldOffsetOf(kSchemas[1], "FactionGroupMask") == 20);
static_assert(schemaFieldOffsetOf(kSchemas[1], "LiquidTypeID")     == 21);
static_assert(schemaFieldOffsetOf(kSchemas[1], "MinElevation")     == 25);
static_assert(schemaFieldOffsetOf(kSchemas[1], "Ambient_multiplier") == 26);
static_assert(schemaFieldOffsetOf(kSchemas[1], "Lightid")          == 27);
static_assert(schemaFieldOffsetOf(kSchemas[0], "MapName_lang")     == 4);   // mapEntry name
static_assert(schemaFieldOffsetOf(kSchemas[3], "CreatureModelScale") == 4); // creatureDisplayInfoEntry
static_assert(schemaFieldOffsetOf(kSchemas[4], "ModelName")        == 2);   // creatureModelDataEntry
static_assert(schemaFieldOffsetOf(kSchemas[6], "InventoryIcon")    == 5);   // itemDisplayInfoEntry
static_assert(schemaFieldOffsetOf(kSchemas[8], "Density")          == 9);   // groundEffectTextureEntry
static_assert(schemaFieldOffsetOf(kSchemas[10], "LightParamsID")   == 7);   // lightEntry
static_assert(schemaFieldOffsetOf(kSchemas[12], "Color")           == 18);  // lightIntBandEntry

bool equalsCI(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

}  // namespace

const TableSchema* findSchema(std::string_view dbcName) {
    for (const TableSchema& s : kSchemas)
        if (equalsCI(dbcName, s.dbcName)) return &s;
    return nullptr;
}

size_t schemaCount() { return kSchemaCount; }

const TableSchema& schemaAt(size_t i) { return kSchemas[i]; }

MapEntry mapEntry(const Dbc& dbc, uint32_t rec) {
    MapEntry e;
    e.id           = dbc.getU32(rec, 0);
    e.directory    = dbc.getString(rec, 1);
    e.instanceType = dbc.getU32(rec, 2);
    e.name         = dbc.getString(rec, 4);   // enUS
    return e;
}

AreaEntry areaEntry(const Dbc& dbc, uint32_t rec) {
    // Rows from older builds can be shorter than the 28-field 5875 layout;
    // Dbc::getU32 bounds-checks against the parsed field count and reads any
    // missing field as 0, so the trailing members degrade to their defaults.
    AreaEntry e;
    e.id               = dbc.getU32(rec, 0);
    e.mapId            = dbc.getU32(rec, 1);
    e.parentAreaId     = dbc.getU32(rec, 2);
    e.areaBit          = dbc.getU32(rec, 3);
    e.flags            = dbc.getU32(rec, 4);
    e.explorationLevel = static_cast<int32_t>(dbc.getU32(rec, 10));
    e.name             = dbc.getString(rec, 11);  // enUS
    e.factionGroupMask = dbc.getU32(rec, 20);
    for (uint32_t i = 0; i < e.liquidTypeId.size(); ++i)
        e.liquidTypeId[i] = dbc.getU32(rec, 21 + i);
    e.minElevation      = fieldF32(dbc, rec, 25);
    e.ambientMultiplier = fieldF32(dbc, rec, 26);
    e.lightId           = dbc.getU32(rec, 27);
    return e;
}

LiquidTypeEntry liquidTypeEntry(const Dbc& dbc, uint32_t rec) {
    LiquidTypeEntry e;
    e.id       = dbc.getU32(rec, 0);
    e.name     = dbc.getString(rec, 1);   // "Water"/"Ocean"/"Magma"/"Slime"
    e.liquidId = dbc.getU32(rec, 1);      // raw string-block offset of the name
    e.type     = dbc.getU32(rec, 2);
    e.spellId  = dbc.getU32(rec, 3);
    return e;
}

uint32_t liquidTypeVanillaToWrath(uint32_t vanillaType, bool ocean) {
    switch (vanillaType) {
        case 0:  return 2;               // magma
        case 2:  return 3;               // slime
        case 3:  return ocean ? 1 : 0;   // water/ocean share vanilla 3
        default: return 0;               // unknown -> water
    }
}

uint32_t liquidTypeWrathToVanilla(uint32_t wrathType) {
    switch (wrathType) {
        case 0:                          // water
        case 1:  return 3;               // ocean collapses onto vanilla 3
        case 2:  return 0;               // magma
        case 3:  return 2;               // slime
        default: return 3;               // unknown -> water
    }
}

LightEntry lightEntry(const Dbc& dbc, uint32_t rec) {
    LightEntry e;
    e.id           = dbc.getU32(rec, 0);
    e.mapId        = dbc.getU32(rec, 1);
    e.x            = fieldF32(dbc, rec, 2);
    e.y            = fieldF32(dbc, rec, 3);
    e.z            = fieldF32(dbc, rec, 4);
    e.falloffStart = fieldF32(dbc, rec, 5);
    e.falloffEnd   = fieldF32(dbc, rec, 6);
    for (uint32_t i = 0; i < e.lightParams.size(); ++i) e.lightParams[i] = dbc.getU32(rec, 7 + i);
    return e;
}

LightParamsRecord lightParamsRecord(const Dbc& dbc, uint32_t rec) {
    LightParamsRecord e;
    e.id                = dbc.getU32(rec, 0);
    e.highlightSky      = dbc.getU32(rec, 1);
    e.skyboxId          = dbc.getU32(rec, 2);
    e.glow              = fieldF32(dbc, rec, 3);
    e.waterShallowAlpha = fieldF32(dbc, rec, 4);
    e.waterDeepAlpha    = fieldF32(dbc, rec, 5);
    e.oceanShallowAlpha = fieldF32(dbc, rec, 6);
    e.oceanDeepAlpha    = fieldF32(dbc, rec, 7);
    return e;
}

LightIntBandEntry lightIntBandEntry(const Dbc& dbc, uint32_t rec) {
    LightIntBandEntry e;
    e.id  = dbc.getU32(rec, 0);
    e.num = std::min<uint32_t>(dbc.getU32(rec, 1), 16);   // never trust the file
    for (uint32_t i = 0; i < 16; ++i) {
        e.times[i]  = dbc.getU32(rec, 2 + i);
        e.colors[i] = dbc.getU32(rec, 18 + i);
    }
    return e;
}

LightFloatBandEntry lightFloatBandEntry(const Dbc& dbc, uint32_t rec) {
    LightFloatBandEntry e;
    e.id  = dbc.getU32(rec, 0);
    e.num = std::min<uint32_t>(dbc.getU32(rec, 1), 16);   // never trust the file
    for (uint32_t i = 0; i < 16; ++i) {
        e.times[i]  = dbc.getU32(rec, 2 + i);
        e.values[i] = fieldF32(dbc, rec, 18 + i);
    }
    return e;
}

LightSkyboxEntry lightSkyboxEntry(const Dbc& dbc, uint32_t rec) {
    LightSkyboxEntry e;
    e.id    = dbc.getU32(rec, 0);
    e.path  = dbc.getString(rec, 1);
    e.flags = dbc.getU32(rec, 2);   // TBC+ column; vanilla rows read as 0
    return e;
}

namespace {

// Bracketing keys of wrapped day-tick `t` (already reduced modulo 2880) among
// the first `n >= 2` (assumed ascending) times: returns the two key indices and
// the 0..1 lerp factor between them. Times outside [times[0], times[n-1])
// fall in the wrapped last->first segment across midnight.
struct BandSpan { uint32_t i0, i1; float f; };

BandSpan bandSpan(const std::array<uint32_t, 16>& times, uint32_t n, uint32_t t) {
    const uint32_t last = n - 1;
    if (t < times[0] || t >= times[last]) {
        // Wrapped segment: last key -> first key across midnight.
        const uint32_t span = (kHalfMinutesPerDay - times[last]) + times[0];
        if (span == 0) return {last, 0, 0.0f};   // degenerate: keys touch at wrap
        const uint32_t dt = t >= times[last] ? t - times[last]
                                             : t + kHalfMinutesPerDay - times[last];
        return {last, 0, static_cast<float>(dt) / static_cast<float>(span)};
    }
    for (uint32_t i = 0; i < last; ++i) {
        if (t >= times[i] && t < times[i + 1]) {
            const uint32_t span = times[i + 1] - times[i];
            const float f = span ? static_cast<float>(t - times[i]) / static_cast<float>(span)
                                 : 0.0f;
            return {i, i + 1, f};
        }
    }
    return {last, last, 0.0f};   // unreachable for ascending keys
}

// Per-byte-channel lerp of two packed 0x00RRGGBB colours (rounds to nearest).
uint32_t lerpPackedColor(uint32_t a, uint32_t b, float f) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = static_cast<float>((a >> shift) & 0xFF);
        const float cb = static_cast<float>((b >> shift) & 0xFF);
        const uint32_t c = static_cast<uint32_t>(std::lround(ca + (cb - ca) * f));
        out |= (c & 0xFF) << shift;
    }
    return out;
}

}  // namespace

uint32_t sampleIntBand(const LightIntBandEntry& band, uint32_t timeHalfMin) {
    const uint32_t n = std::min<uint32_t>(band.num, 16);
    if (n == 0) return 0;                       // absent band -> black
    if (n == 1) return band.colors[0];          // single key -> constant
    const BandSpan s = bandSpan(band.times, n, timeHalfMin % kHalfMinutesPerDay);
    return lerpPackedColor(band.colors[s.i0], band.colors[s.i1], s.f);
}

float sampleFloatBand(const LightFloatBandEntry& band, uint32_t timeHalfMin) {
    const uint32_t n = std::min<uint32_t>(band.num, 16);
    if (n == 0) return 0.0f;                    // absent band
    if (n == 1) return band.values[0];          // single key -> constant
    const BandSpan s = bandSpan(band.times, n, timeHalfMin % kHalfMinutesPerDay);
    return band.values[s.i0] + (band.values[s.i1] - band.values[s.i0]) * s.f;
}

float lightWeight(float dist, float falloffStart, float falloffEnd) {
    if (dist <= falloffStart) return 1.0f;
    if (dist >= falloffEnd)   return 0.0f;
    // Reaching here implies falloffStart < dist < falloffEnd, so the span > 0.
    return (falloffEnd - dist) / (falloffEnd - falloffStart);
}

CreatureModelDataEntry creatureModelDataEntry(const Dbc& dbc, uint32_t rec) {
    CreatureModelDataEntry e;
    e.id        = dbc.getU32(rec, 0);
    e.modelPath = dbc.getString(rec, 2);
    return e;
}

CreatureDisplayInfoEntry creatureDisplayInfoEntry(const Dbc& dbc, uint32_t rec) {
    CreatureDisplayInfoEntry e;
    e.id      = dbc.getU32(rec, 0);
    e.modelId = dbc.getU32(rec, 1);
    e.scale   = fieldF32(dbc, rec, 4);   // VERIFY-FLAGGED: field index less certain
    return e;
}

GameObjectDisplayInfoEntry gameObjectDisplayInfoEntry(const Dbc& dbc, uint32_t rec) {
    GameObjectDisplayInfoEntry e;
    e.id        = dbc.getU32(rec, 0);
    e.modelName = dbc.getString(rec, 1);
    return e;
}

GroundEffectDoodadEntry groundEffectDoodadEntry(const Dbc& dbc, uint32_t rec) {
    GroundEffectDoodadEntry e;
    e.id        = dbc.getU32(rec, 0);
    e.modelPath = dbc.getString(rec, 1);
    return e;
}

GroundEffectTextureEntry groundEffectTextureEntry(const Dbc& dbc, uint32_t rec) {
    GroundEffectTextureEntry e;
    e.id = dbc.getU32(rec, 0);
    for (uint32_t i = 0; i < e.doodadIds.size(); ++i) e.doodadIds[i] = dbc.getU32(rec, 1 + i);
    e.density = dbc.getU32(rec, 9);   // VERIFY-FLAGGED: field index less certain
    return e;
}

std::string normalizeModelPath(const std::string& dbcPath) {
    if (endsWithCI(dbcPath, ".mdx") || endsWithCI(dbcPath, ".mdl"))
        return dbcPath.substr(0, dbcPath.size() - 4) + ".m2";
    return dbcPath;
}

ItemDisplayInfoEntry itemDisplayInfoEntry(const Dbc& dbc, uint32_t rec) {
    ItemDisplayInfoEntry e;
    e.id              = dbc.getU32(rec, 0);
    e.modelName[0]    = dbc.getString(rec, 1);
    e.modelName[1]    = dbc.getString(rec, 2);
    e.modelTexture[0] = dbc.getString(rec, 3);
    e.modelTexture[1] = dbc.getString(rec, 4);
    e.inventoryIcon   = dbc.getString(rec, 5);
    return e;
}

void ItemDisplayDb::build(const Dbc& dbc) {
    for (uint32_t r = 0; r < dbc.recordCount(); ++r) {
        ItemDisplayInfoEntry e = itemDisplayInfoEntry(dbc, r);
        byId_[e.id] = e;
    }
}
const ItemDisplayInfoEntry* ItemDisplayDb::get(uint32_t displayId) const {
    auto it = byId_.find(displayId);
    return it == byId_.end() ? nullptr : &it->second;
}
std::string ItemDisplayDb::icon(uint32_t displayId) const {
    const ItemDisplayInfoEntry* e = get(displayId);
    return e ? e->inventoryIcon : std::string();
}

EmotesEntry emotesEntry(const Dbc& dbc, uint32_t rec) {
    EmotesEntry e;
    e.id         = dbc.getU32(rec, 0);
    e.name       = dbc.getString(rec, 1);
    e.animId     = dbc.getU32(rec, 2);
    e.flags      = dbc.getU32(rec, 3);
    e.emoteType  = dbc.getU32(rec, 4);
    e.standState = dbc.getU32(rec, 5);
    e.soundId    = dbc.getU32(rec, 6);
    return e;
}

EmotesTextEntry emotesTextEntry(const Dbc& dbc, uint32_t rec) {
    EmotesTextEntry e;
    e.id      = dbc.getU32(rec, 0);
    e.name    = dbc.getString(rec, 1);
    e.emoteId = dbc.getU32(rec, 2);
    return e;
}

namespace {
std::string lowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

void EmoteDb::build(const Dbc* emotes, const Dbc* emotesText) {
    if (emotes) {
        for (uint32_t r = 0; r < emotes->recordCount(); ++r) {
            EmotesEntry e = emotesEntry(*emotes, r);
            emotes_[e.id] = e;
        }
    }
    if (emotesText) {
        for (uint32_t r = 0; r < emotesText->recordCount(); ++r) {
            EmotesTextEntry e = emotesTextEntry(*emotesText, r);
            if (!e.name.empty()) byCommand_[lowerStr(e.name)] = e.emoteId;
        }
    }
}

int EmoteDb::commandToTextEmote(const std::string& command) const {
    auto it = byCommand_.find(lowerStr(command));
    return it == byCommand_.end() ? -1 : static_cast<int>(it->second);
}

const EmotesEntry* EmoteDb::emote(uint32_t emoteId) const {
    auto it = emotes_.find(emoteId);
    return it == emotes_.end() ? nullptr : &it->second;
}

FactionTemplateEntry factionTemplateEntry(const Dbc& dbc, uint32_t rec) {
    FactionTemplateEntry e;
    e.id         = dbc.getU32(rec, 0);
    e.faction    = dbc.getU32(rec, 1);
    e.flags      = dbc.getU32(rec, 2);
    e.ourMask    = dbc.getU32(rec, 3);
    e.friendMask = dbc.getU32(rec, 4);
    e.enemyMask  = dbc.getU32(rec, 5);
    for (int i = 0; i < 4; ++i) e.enemies[i] = dbc.getU32(rec, 6 + i);
    for (int i = 0; i < 4; ++i) e.friends[i] = dbc.getU32(rec, 10 + i);
    return e;
}

bool factionIsHostile(const FactionTemplateEntry& a, const FactionTemplateEntry& b) {
    if (b.faction) {
        for (uint32_t e : a.enemies) if (e && e == b.faction) return true;
        for (uint32_t f : a.friends) if (f && f == b.faction) return false;
    }
    return (a.enemyMask & b.ourMask) != 0;
}

bool factionIsFriendly(const FactionTemplateEntry& a, const FactionTemplateEntry& b) {
    if (b.faction) {
        for (uint32_t e : a.enemies) if (e && e == b.faction) return false;
        for (uint32_t f : a.friends) if (f && f == b.faction) return true;
    }
    return (a.friendMask & b.ourMask) != 0;
}

FactionReaction factionReaction(const FactionTemplateEntry& a, const FactionTemplateEntry& b) {
    if (factionIsHostile(a, b))  return FactionReaction::Hostile;   // hostile wins
    if (factionIsFriendly(a, b)) return FactionReaction::Friendly;
    return FactionReaction::Neutral;
}

void FactionTemplateDb::build(const Dbc& dbc) {
    for (uint32_t r = 0; r < dbc.recordCount(); ++r) {
        FactionTemplateEntry e = factionTemplateEntry(dbc, r);
        byId_[e.id] = e;
    }
}
const FactionTemplateEntry* FactionTemplateDb::get(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &it->second;
}
FactionReaction FactionTemplateDb::reaction(uint32_t aId, uint32_t bId) const {
    const FactionTemplateEntry* a = get(aId);
    const FactionTemplateEntry* b = get(bId);
    if (!a || !b) return FactionReaction::Neutral;
    return factionReaction(*a, *b);
}

SpellCastTimesEntry spellCastTimesEntry(const Dbc& dbc, uint32_t rec) {
    SpellCastTimesEntry e;
    e.id       = dbc.getU32(rec, 0);
    e.baseMs   = static_cast<int32_t>(dbc.getU32(rec, 1));
    e.perLevel = static_cast<int32_t>(dbc.getU32(rec, 2));
    e.minMs    = static_cast<int32_t>(dbc.getU32(rec, 3));
    return e;
}

SpellDurationEntry spellDurationEntry(const Dbc& dbc, uint32_t rec) {
    SpellDurationEntry e;
    e.id       = dbc.getU32(rec, 0);
    e.baseMs   = static_cast<int32_t>(dbc.getU32(rec, 1));
    e.perLevel = static_cast<int32_t>(dbc.getU32(rec, 2));
    e.maxMs    = static_cast<int32_t>(dbc.getU32(rec, 3));
    return e;
}

SpellRadiusEntry spellRadiusEntry(const Dbc& dbc, uint32_t rec) {
    SpellRadiusEntry e;
    e.id        = dbc.getU32(rec, 0);
    e.radius    = fieldF32(dbc, rec, 1);
    e.perLevel  = fieldF32(dbc, rec, 2);
    e.maxRadius = fieldF32(dbc, rec, 3);
    return e;
}

SpellRangeEntry spellRangeEntry(const Dbc& dbc, uint32_t rec) {
    SpellRangeEntry e;
    e.id       = dbc.getU32(rec, 0);
    e.minRange = fieldF32(dbc, rec, 1);
    e.maxRange = fieldF32(dbc, rec, 2);
    e.flags    = dbc.getU32(rec, 3);
    return e;
}

void SpellSupportDb::build(const Dbc* ct, const Dbc* dur, const Dbc* rad, const Dbc* rng) {
    if (ct)  for (uint32_t r = 0; r < ct->recordCount();  ++r) { auto e = spellCastTimesEntry(*ct, r);  castTimes_[e.id] = e; }
    if (dur) for (uint32_t r = 0; r < dur->recordCount(); ++r) { auto e = spellDurationEntry(*dur, r);  durations_[e.id] = e; }
    if (rad) for (uint32_t r = 0; r < rad->recordCount(); ++r) { auto e = spellRadiusEntry(*rad, r);    radii_[e.id] = e; }
    if (rng) for (uint32_t r = 0; r < rng->recordCount(); ++r) { auto e = spellRangeEntry(*rng, r);     ranges_[e.id] = e; }
}

const SpellCastTimesEntry* SpellSupportDb::castTime(uint32_t id) const {
    auto it = castTimes_.find(id); return it == castTimes_.end() ? nullptr : &it->second;
}
const SpellDurationEntry* SpellSupportDb::duration(uint32_t id) const {
    auto it = durations_.find(id); return it == durations_.end() ? nullptr : &it->second;
}
const SpellRadiusEntry* SpellSupportDb::radius(uint32_t id) const {
    auto it = radii_.find(id); return it == radii_.end() ? nullptr : &it->second;
}
const SpellRangeEntry* SpellSupportDb::range(uint32_t id) const {
    auto it = ranges_.find(id); return it == ranges_.end() ? nullptr : &it->second;
}

CharHairGeosetEntry charHairGeosetEntry(const Dbc& dbc, uint32_t rec) {
    CharHairGeosetEntry e;
    e.id        = dbc.getU32(rec, 0);
    e.raceId    = dbc.getU32(rec, 1);
    e.sexId     = dbc.getU32(rec, 2);
    e.variation = dbc.getU32(rec, 3);
    e.geosetId  = dbc.getU32(rec, 4);
    e.showScalp = dbc.getU32(rec, 5) != 0;
    return e;
}

void HairGeosetResolver::build(const Dbc& dbc) {
    for (uint32_t r = 0; r < dbc.recordCount(); ++r) {
        CharHairGeosetEntry e = charHairGeosetEntry(dbc, r);
        byKey_[key(e.raceId, e.sexId, e.variation)] = e;
    }
}

int HairGeosetResolver::hairGeoset(uint32_t race, uint32_t sex, uint32_t variation) const {
    auto it = byKey_.find(key(race, sex, variation));
    return it == byKey_.end() ? -1 : static_cast<int>(it->second.geosetId);
}

bool HairGeosetResolver::showScalp(uint32_t race, uint32_t sex, uint32_t variation) const {
    auto it = byKey_.find(key(race, sex, variation));
    return it != byKey_.end() && it->second.showScalp;
}

uint32_t HairGeosetResolver::variationCount(uint32_t race, uint32_t sex) const {
    uint32_t n = 0;
    for (const auto& kv : byKey_)
        if (kv.second.raceId == race && kv.second.sexId == sex) ++n;
    return n;
}

} // namespace wf
