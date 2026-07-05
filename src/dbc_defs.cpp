#include "dbc_defs.hpp"

#include <cctype>
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

MapEntry mapEntry(const Dbc& dbc, uint32_t rec) {
    MapEntry e;
    e.id           = dbc.getU32(rec, 0);
    e.directory    = dbc.getString(rec, 1);
    e.instanceType = dbc.getU32(rec, 2);
    e.name         = dbc.getString(rec, 4);   // enUS
    return e;
}

AreaEntry areaEntry(const Dbc& dbc, uint32_t rec) {
    AreaEntry e;
    e.id               = dbc.getU32(rec, 0);
    e.mapId            = dbc.getU32(rec, 1);
    e.parentAreaId     = dbc.getU32(rec, 2);
    e.areaBit          = dbc.getU32(rec, 3);
    e.flags            = dbc.getU32(rec, 4);
    e.explorationLevel = static_cast<int32_t>(dbc.getU32(rec, 10));
    e.name             = dbc.getString(rec, 11);  // enUS
    return e;
}

LiquidTypeEntry liquidTypeEntry(const Dbc& dbc, uint32_t rec) {
    LiquidTypeEntry e;
    e.id       = dbc.getU32(rec, 0);
    e.liquidId = dbc.getU32(rec, 1);
    e.type     = dbc.getU32(rec, 2);
    e.spellId  = dbc.getU32(rec, 3);
    return e;
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
