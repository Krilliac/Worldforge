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

std::string normalizeModelPath(const std::string& dbcPath) {
    if (endsWithCI(dbcPath, ".mdx") || endsWithCI(dbcPath, ".mdl"))
        return dbcPath.substr(0, dbcPath.size() - 4) + ".m2";
    return dbcPath;
}

} // namespace wf
