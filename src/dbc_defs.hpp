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

// CharHairGeosets.dbc entry. Layout confirmed against the owned 1.12.1 client
// (148 records x 6 fields x 24 bytes; sample rec0 = id 241, race 9, sex 0,
// variation 0, geoset 1, showScalp 0). Maps a (race, sex, hair-style variation)
// to the M2 geoset (skin-section) id to enable for that hair -- the hair sits in
// geoset group 0 (ids 1..99), which composes with T2.2 selectGeosets (base id 0
// always shown + the chosen hair geoset).
struct CharHairGeosetEntry {
    uint32_t id        = 0;   // field 0
    uint32_t raceId    = 0;   // field 1
    uint32_t sexId     = 0;   // field 2 (0 = male, 1 = female)
    uint32_t variation = 0;   // field 3 (hair-style index)
    uint32_t geosetId  = 0;   // field 4 (M2 geoset id to show for this hair)
    bool     showScalp = false; // field 5 (bald scalp vs hair mesh)
};

// ---- ItemDisplayInfo.dbc: item icon / model resolution ---------------------
// The client render table item templates point at via displayId (see
// net/item_query.hpp ItemQueryResponse::displayId). Vanilla 1.12.1 layout is 23
// fields x 92 bytes (dbc_probe): id, modelName[2], modelTexture[2],
// inventoryIcon[2], geosetGroup[3], flags, spellVisualId, groupSound,
// helmetGeoset[2], texture[8]. The field ORDER matches the mangos
// ItemDisplayInfoEntry struct (id, model, modelTexture, inventoryIcon, ...); the
// primary inventory icon is field 5. We expose the fields a 2D editor actually
// needs -- the icon plus the world model names.
struct ItemDisplayInfoEntry {
    uint32_t    id = 0;              // field 0
    std::string modelName[2];       // fields 1..2 (left/right .mdx)
    std::string modelTexture[2];    // fields 3..4
    std::string inventoryIcon;      // field 5 (the icon BLP name, no extension)
};
ItemDisplayInfoEntry itemDisplayInfoEntry(const Dbc& dbc, uint32_t rec);

// Index over ItemDisplayInfo.dbc by id -> resolve an item's displayId to its
// inventory icon (and world model). icon(displayId) returns "" if unknown.
class ItemDisplayDb {
public:
    void build(const Dbc& itemDisplayInfo);
    const ItemDisplayInfoEntry* get(uint32_t displayId) const;
    std::string icon(uint32_t displayId) const;   // "" if unknown / no icon
    size_t size() const { return byId_.size(); }
private:
    std::unordered_map<uint32_t, ItemDisplayInfoEntry> byId_;
};

// ---- Emotes.dbc / EmotesText.dbc: the /emote system ------------------------
// Layout from mangos-zero DBCStructure (Emotes "nxxxixx" 7 fields; EmotesText
// "nxixx.." 19 fields), cross-checked vs the owned client. Emotes maps an emote
// id to the animation + sound to play; EmotesText maps a /command word to the
// text-emote id the client sends in CMSG_TEXT_EMOTE (see net/text_emote.hpp).
struct EmotesEntry {
    uint32_t    id            = 0;   // field 0
    std::string name;                // field 1 (slash command / internal name)
    uint32_t    animId        = 0;   // field 2 (animation to play)
    uint32_t    flags         = 0;   // field 3
    uint32_t    emoteType     = 0;   // field 4 (spec proc)
    uint32_t    standState    = 0;   // field 5
    uint32_t    soundId       = 0;   // field 6 (event sound)
};
struct EmotesTextEntry {
    uint32_t    id       = 0;        // field 0
    std::string name;                // field 1 (the /command word, e.g. "dance")
    uint32_t    emoteId  = 0;        // field 2 (the text-emote id -> CMSG_TEXT_EMOTE)
};
EmotesEntry     emotesEntry(const Dbc& dbc, uint32_t rec);
EmotesTextEntry emotesTextEntry(const Dbc& dbc, uint32_t rec);

// Resolves the /emote pipeline: a command word -> its text-emote id (what the
// client sends), and an emote id -> its Emotes row (animation + sound to play).
class EmoteDb {
public:
    void build(const Dbc* emotes, const Dbc* emotesText);
    // Text-emote id for a /command word (case-insensitive), or -1 if unknown.
    int  commandToTextEmote(const std::string& command) const;
    // Emotes row for an emote id (animation/sound), or nullptr.
    const EmotesEntry* emote(uint32_t emoteId) const;
    size_t emoteCount() const { return emotes_.size(); }
    size_t commandCount() const { return byCommand_.size(); }
private:
    std::unordered_map<uint32_t, EmotesEntry> emotes_;      // emote id -> row
    std::unordered_map<std::string, uint32_t> byCommand_;   // lower(cmd) -> textEmote
};

// ---- FactionTemplate.dbc: creature reaction system -------------------------
// Layout verified vs the owned 1.12.1 client (dbc_probe: 239 rec x 14 x 56):
// id, faction, flags, ourMask (our faction-group bits), friendMask/enemyMask
// (group bits we like/hate), then enemies[4] + friends[4] (specific faction ids).
// Two units' reaction is decided by comparing their templates (see below) -- the
// alpha ObjectClient reaction path.
struct FactionTemplateEntry {
    uint32_t id         = 0;   // field 0
    uint32_t faction    = 0;   // field 1 (Faction.dbc id; 0 = none)
    uint32_t flags      = 0;   // field 2
    uint32_t ourMask    = 0;   // field 3 (faction-group bits WE belong to)
    uint32_t friendMask = 0;   // field 4 (groups we're friendly toward)
    uint32_t enemyMask  = 0;   // field 5 (groups we're hostile toward)
    std::array<uint32_t, 4> enemies{};   // fields 6..9  (specific enemy factions)
    std::array<uint32_t, 4> friends{};   // fields 10..13 (specific friend factions)
};
FactionTemplateEntry factionTemplateEntry(const Dbc& dbc, uint32_t rec);

// Reaction of template `a` toward template `b` (asymmetric, like the client).
// Specific enemy/friend lists win over the group masks; hostile is checked before
// friendly. Reproduces mangos FactionTemplateEntry::IsHostileTo/IsFriendlyTo.
bool factionIsHostile(const FactionTemplateEntry& a, const FactionTemplateEntry& b);
bool factionIsFriendly(const FactionTemplateEntry& a, const FactionTemplateEntry& b);
enum class FactionReaction { Hostile, Neutral, Friendly };
FactionReaction factionReaction(const FactionTemplateEntry& a, const FactionTemplateEntry& b);

// Index over FactionTemplate.dbc by id; reaction(aId, bId) resolves both and
// answers Neutral if either id is unknown.
class FactionTemplateDb {
public:
    void build(const Dbc& factionTemplate);
    const FactionTemplateEntry* get(uint32_t id) const;
    FactionReaction reaction(uint32_t aId, uint32_t bId) const;
    size_t size() const { return byId_.size(); }
private:
    std::unordered_map<uint32_t, FactionTemplateEntry> byId_;
};

// ---- spell-support DBCs (the tables Spell.dbc indexes by *Index) ------------
// Small fixed-layout tables verified against the owned 1.12.1 client (dbc_probe):
// each is ID + three values, all 16 bytes / 4 fields (SpellRange is wider but we
// read only its leading numeric fields). These resolve a spell's rangeIndex /
// castingTimeIndex / durationIndex / radius id to real numbers for tooltips.
struct SpellCastTimesEntry {   // SpellCastTimes.dbc (50 rec x 4 x 16)
    uint32_t id      = 0;      // field 0
    int32_t  baseMs  = 0;      // field 1 (base cast time, ms)
    int32_t  perLevel = 0;     // field 2 (per-level delta, ms)
    int32_t  minMs   = 0;      // field 3 (minimum cast time, ms)
};
struct SpellDurationEntry {    // SpellDuration.dbc (70 rec x 4 x 16)
    uint32_t id         = 0;   // field 0
    int32_t  baseMs     = 0;   // field 1 (base duration, ms)
    int32_t  perLevel   = 0;   // field 2 (per-level delta, ms)
    int32_t  maxMs      = 0;   // field 3 (max duration, ms)
};
struct SpellRadiusEntry {      // SpellRadius.dbc (15 rec x 4 x 16), floats
    uint32_t id        = 0;    // field 0
    float    radius    = 0.0f; // field 1 (yards)
    float    perLevel  = 0.0f; // field 2
    float    maxRadius = 0.0f; // field 3
};
struct SpellRangeEntry {       // SpellRange.dbc (25 rec x 22 x 88), leading fields
    uint32_t id       = 0;     // field 0
    float    minRange = 0.0f;  // field 1 (yards)
    float    maxRange = 0.0f;  // field 2 (yards)
    uint32_t flags    = 0;     // field 3
};

SpellCastTimesEntry spellCastTimesEntry(const Dbc& dbc, uint32_t rec);
SpellDurationEntry  spellDurationEntry(const Dbc& dbc, uint32_t rec);
SpellRadiusEntry    spellRadiusEntry(const Dbc& dbc, uint32_t rec);
SpellRangeEntry     spellRangeEntry(const Dbc& dbc, uint32_t rec);

// Indexes the four spell-support tables by their id so a spell's *Index fields
// resolve to real numbers. Build once from the parsed DBCs; missing ids return
// a null pointer.
class SpellSupportDb {
public:
    void build(const Dbc* castTimes, const Dbc* durations,
               const Dbc* radii, const Dbc* ranges);
    const SpellCastTimesEntry* castTime(uint32_t id) const;
    const SpellDurationEntry*  duration(uint32_t id) const;
    const SpellRadiusEntry*    radius(uint32_t id) const;
    const SpellRangeEntry*     range(uint32_t id) const;
    size_t size() const { return castTimes_.size() + durations_.size()
                               + radii_.size() + ranges_.size(); }
private:
    std::unordered_map<uint32_t, SpellCastTimesEntry> castTimes_;
    std::unordered_map<uint32_t, SpellDurationEntry>  durations_;
    std::unordered_map<uint32_t, SpellRadiusEntry>    radii_;
    std::unordered_map<uint32_t, SpellRangeEntry>     ranges_;
};

// Typed accessors. `rec` is a 0-based record index in `[0, dbc.recordCount())`.
MapEntry        mapEntry(const Dbc& dbc, uint32_t rec);
CharHairGeosetEntry charHairGeosetEntry(const Dbc& dbc, uint32_t rec);
AreaEntry       areaEntry(const Dbc& dbc, uint32_t rec);
LiquidTypeEntry liquidTypeEntry(const Dbc& dbc, uint32_t rec);
LightEntry      lightEntry(const Dbc& dbc, uint32_t rec);
CreatureModelDataEntry     creatureModelDataEntry(const Dbc& dbc, uint32_t rec);
CreatureDisplayInfoEntry   creatureDisplayInfoEntry(const Dbc& dbc, uint32_t rec);
GameObjectDisplayInfoEntry gameObjectDisplayInfoEntry(const Dbc& dbc, uint32_t rec);
GroundEffectDoodadEntry    groundEffectDoodadEntry(const Dbc& dbc, uint32_t rec);
GroundEffectTextureEntry   groundEffectTextureEntry(const Dbc& dbc, uint32_t rec);

// Index over CharHairGeosets.dbc: resolve a character's (race, sex, hair-style
// variation) to the hair geoset id to enable (T3.3 character assembly, built on
// the T2.2 geoset model). Build once from the parsed DBC; a miss returns -1.
class HairGeosetResolver {
public:
    void build(const Dbc& charHairGeosets);
    // Hair geoset id for (race, sex, variation), or -1 if absent. The caller adds
    // this id to the character M2's visible geoset set (group 0 = hair).
    int hairGeoset(uint32_t race, uint32_t sex, uint32_t variation) const;
    // showScalp flag for the same key (false if absent) -- a bald/scalp variation.
    bool showScalp(uint32_t race, uint32_t sex, uint32_t variation) const;
    // Number of distinct hair-style variations for (race, sex) -- for a UI/count.
    uint32_t variationCount(uint32_t race, uint32_t sex) const;
    size_t size() const { return byKey_.size(); }

private:
    static uint64_t key(uint32_t race, uint32_t sex, uint32_t variation) {
        return (uint64_t(race) << 40) | (uint64_t(sex) << 32) | variation;
    }
    std::unordered_map<uint64_t, CharHairGeosetEntry> byKey_;
};

// Normalise a DBC model path to the on-disk file: vanilla DBCs reference models
// with a .mdx (or .mdl) extension but the archive stores .m2 (MD20). Replaces a
// trailing .mdx/.mdl with .m2 (case-insensitive); leaves .wmo and others as-is.
std::string normalizeModelPath(const std::string& dbcPath);

} // namespace wf
