#pragma once
// ---------------------------------------------------------------------------
// Item query codec for vanilla 1.12.1 (build 5875). When the client encounters
// an item entry it has no cached template for (a link in chat, a loot roll, a
// vendor slot) it sends CMSG_ITEM_QUERY_SINGLE and caches the response in its
// local item.wdb. This reconstructs that exchange.
//
//   client -> CMSG_ITEM_QUERY_SINGLE          { u32 entry, u64 guid }
//   server -> SMSG_ITEM_QUERY_SINGLE_RESPONSE { full ItemTemplate, see below }
//
// Opcode values (vanilla 1.12.1, from mangos-zero Opcodes.h):
//   CMSG_ITEM_QUERY_SINGLE          = 0x0056
//   SMSG_ITEM_QUERY_SINGLE_RESPONSE = 0x0058
//
// Wire layout of the response (verified vs mangos-zero HandleItemQuerySingle):
//   u32 entry
//   u32 itemClass
//   u32 subClass
//   cstr name1                 -- the item name
//   u8  0x00                   -- name2: always an empty string (bare NUL)
//   u8  0x00                   -- name3: always an empty string (bare NUL)
//   u8  0x00                   -- name4: always an empty string (bare NUL)
//   u32 displayId
//   u32 quality
//   u32 flags
//   u32 buyPrice
//   u32 sellPrice
//   u32 inventoryType
//   u32 allowableClass         -- class mask (-1 = all)
//   u32 allowableRace          -- race mask  (-1 = all)
//   u32 itemLevel
//   u32 requiredLevel
//   u32 requiredSkill
//   u32 requiredSkillRank
//   u32 requiredSpell
//   u32 requiredHonorRank
//   u32 requiredCityRank
//   u32 requiredReputationFaction
//   u32 requiredReputationRank
//   u32 maxCount
//   u32 stackable
//   u32 containerSlots
//   stats[10]  : { u32 statType, i32 statValue }          -- MAX_ITEM_PROTO_STATS
//   damages[5] : { f32 min, f32 max, u32 school }         -- MAX_ITEM_PROTO_DAMAGES
//   u32 armor
//   u32 holyRes, u32 fireRes, u32 natureRes, u32 frostRes, u32 shadowRes, u32 arcaneRes
//   u32 delay
//   u32 ammoType
//   f32 rangedModRange
//   spells[5]  : { u32 spellId, u32 trigger, i32 charges,
//                  i32 cooldown, u32 category, i32 categoryCooldown } -- MAX_ITEM_PROTO_SPELLS
//   u32 bonding
//   cstr description
//   u32 pageText
//   u32 languageId
//   u32 pageMaterial
//   u32 startQuest
//   u32 lockId
//   u32 material
//   u32 sheath
//   u32 randomProperty
//   u32 block
//   u32 itemSet
//   u32 maxDurability
//   u32 area
//   u32 map                    -- added in the 1.12.x client branch
//   u32 bagFamily
//
// Vanilla-specific traits:
//   * Every scalar template field is a u32 (or f32) on the wire -- vanilla does
//     NOT pack any of these into u8/u16 the way later expansions do.
//   * name2/name3/name4 are transmitted as three *bare NUL bytes* (empty
//     CStrings), never populated -- Blizzard only ever sent name1.
//   * ObjectGuid in the request is a raw little-endian u64 (not packed).
//   * Strings are plain NUL-terminated CStrings (no length prefix).
//   * A "not found" reply is a single u32 == (entry | 0x80000000); see
//     encodeItemQueryNotFound below.
//
// Pure body codec over byte_reader/byte_writer -- no opcode enums referenced
// here (values live only in the comment above). Cross-checked vs the GPL
// mangos-zero vanilla source used as a fact reference; no code copied.
// Round-trips deterministically (tests/test_item_query.cpp).
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"   // writeCString / readCString (reused, not redefined)

namespace wf {

// Fixed vanilla array sizes (mangos-zero ItemPrototype.h).
constexpr int kItemStatCount   = 10;   // MAX_ITEM_PROTO_STATS
constexpr int kItemDamageCount = 5;    // MAX_ITEM_PROTO_DAMAGES
constexpr int kItemSpellCount  = 5;    // MAX_ITEM_PROTO_SPELLS

// ---- CMSG_ITEM_QUERY_SINGLE (client request) --------------------------------
// The client sends the entry plus the raw u64 guid of the item instance it saw
// (the server ignores the guid for template lookup, but it is on the wire).
struct ItemQueryRequest {
    uint32_t entry = 0;
    uint64_t guid  = 0;
};

inline std::vector<uint8_t> encodeItemQuery(uint32_t entry, uint64_t guid) {
    ByteWriter w;
    w.u32(entry);
    w.u64(guid);
    return w.data();
}

inline ItemQueryRequest decodeItemQuery(ByteReader& r) {
    ItemQueryRequest q;
    q.entry = r.u32();
    q.guid  = r.u64();
    return q;
}

// ---- SMSG_ITEM_QUERY_SINGLE_RESPONSE component structs ----------------------
struct ItemStat {
    uint32_t type  = 0;   // ItemModType (ItemStatType)
    int32_t  value = 0;
};

struct ItemDamage {
    float    min    = 0.0f;
    float    max    = 0.0f;
    uint32_t school = 0;  // damage type / resistance school id
};

struct ItemSpellInfo {
    uint32_t spellId          = 0;
    uint32_t trigger          = 0;   // ItemSpelltriggerType
    int32_t  charges          = 0;   // negative = limited uses (wire value as-is)
    int32_t  cooldown         = -1;
    uint32_t category         = 0;
    int32_t  categoryCooldown = -1;
};

// ---- SMSG_ITEM_QUERY_SINGLE_RESPONSE (the cached ItemTemplate) --------------
struct ItemQueryResponse {
    uint32_t entry         = 0;
    uint32_t itemClass     = 0;   // ItemClass.dbc
    uint32_t subClass      = 0;   // ItemSubClass.dbc
    std::string name;             // name1 (name2/3/4 are always empty on the wire)
    uint32_t displayId     = 0;   // ItemDisplayInfo.dbc
    uint32_t quality       = 0;   // ItemQualities
    uint32_t flags         = 0;
    uint32_t buyPrice      = 0;
    uint32_t sellPrice     = 0;
    uint32_t inventoryType = 0;
    uint32_t allowableClass = 0;  // class mask (0xFFFFFFFF = all)
    uint32_t allowableRace  = 0;  // race mask  (0xFFFFFFFF = all)
    uint32_t itemLevel     = 0;
    uint32_t requiredLevel = 0;
    uint32_t requiredSkill = 0;   // SkillLine.dbc
    uint32_t requiredSkillRank = 0;
    uint32_t requiredSpell = 0;   // Spell.dbc
    uint32_t requiredHonorRank = 0;
    uint32_t requiredCityRank  = 0;
    uint32_t requiredReputationFaction = 0;  // Faction.dbc
    uint32_t requiredReputationRank    = 0;
    uint32_t maxCount      = 0;
    uint32_t stackable     = 0;
    uint32_t containerSlots = 0;
    std::array<ItemStat,   kItemStatCount>   stats{};
    std::array<ItemDamage, kItemDamageCount> damages{};
    uint32_t armor         = 0;
    uint32_t holyRes       = 0;
    uint32_t fireRes       = 0;
    uint32_t natureRes     = 0;
    uint32_t frostRes      = 0;
    uint32_t shadowRes     = 0;
    uint32_t arcaneRes     = 0;
    uint32_t delay         = 0;   // weapon speed, ms
    uint32_t ammoType      = 0;
    float    rangedModRange = 0.0f;
    std::array<ItemSpellInfo, kItemSpellCount> spells{};
    uint32_t bonding       = 0;   // ItemBondingType
    std::string description;
    uint32_t pageText      = 0;
    uint32_t languageId    = 0;   // Languages.dbc
    uint32_t pageMaterial  = 0;
    uint32_t startQuest    = 0;
    uint32_t lockId        = 0;
    uint32_t material      = 0;   // Material.dbc
    uint32_t sheath        = 0;   // SheathType
    uint32_t randomProperty = 0;  // ItemRandomProperties.dbc
    uint32_t block         = 0;
    uint32_t itemSet       = 0;   // ItemSet.dbc
    uint32_t maxDurability = 0;
    uint32_t area          = 0;   // AreaTable.dbc
    uint32_t map           = 0;   // Map.dbc (added in the 1.12.x client branch)
    uint32_t bagFamily     = 0;   // ItemBagFamily bit mask
};

inline std::vector<uint8_t> encodeItemQueryResponse(const ItemQueryResponse& p) {
    ByteWriter w;
    w.u32(p.entry);
    w.u32(p.itemClass);
    w.u32(p.subClass);
    writeCString(w, p.name);
    w.u8(0); w.u8(0); w.u8(0);          // name2, name3, name4: always empty
    w.u32(p.displayId);
    w.u32(p.quality);
    w.u32(p.flags);
    w.u32(p.buyPrice);
    w.u32(p.sellPrice);
    w.u32(p.inventoryType);
    w.u32(p.allowableClass);
    w.u32(p.allowableRace);
    w.u32(p.itemLevel);
    w.u32(p.requiredLevel);
    w.u32(p.requiredSkill);
    w.u32(p.requiredSkillRank);
    w.u32(p.requiredSpell);
    w.u32(p.requiredHonorRank);
    w.u32(p.requiredCityRank);
    w.u32(p.requiredReputationFaction);
    w.u32(p.requiredReputationRank);
    w.u32(p.maxCount);
    w.u32(p.stackable);
    w.u32(p.containerSlots);
    for (const ItemStat& s : p.stats) {
        w.u32(s.type);
        w.i32(s.value);
    }
    for (const ItemDamage& d : p.damages) {
        w.f32(d.min);
        w.f32(d.max);
        w.u32(d.school);
    }
    w.u32(p.armor);
    w.u32(p.holyRes);
    w.u32(p.fireRes);
    w.u32(p.natureRes);
    w.u32(p.frostRes);
    w.u32(p.shadowRes);
    w.u32(p.arcaneRes);
    w.u32(p.delay);
    w.u32(p.ammoType);
    w.f32(p.rangedModRange);
    for (const ItemSpellInfo& sp : p.spells) {
        w.u32(sp.spellId);
        w.u32(sp.trigger);
        w.i32(sp.charges);
        w.i32(sp.cooldown);
        w.u32(sp.category);
        w.i32(sp.categoryCooldown);
    }
    w.u32(p.bonding);
    writeCString(w, p.description);
    w.u32(p.pageText);
    w.u32(p.languageId);
    w.u32(p.pageMaterial);
    w.u32(p.startQuest);
    w.u32(p.lockId);
    w.u32(p.material);
    w.u32(p.sheath);
    w.u32(p.randomProperty);
    w.u32(p.block);
    w.u32(p.itemSet);
    w.u32(p.maxDurability);
    w.u32(p.area);
    w.u32(p.map);
    w.u32(p.bagFamily);
    return w.data();
}

inline ItemQueryResponse decodeItemQueryResponse(ByteReader& r) {
    ItemQueryResponse p;
    p.entry     = r.u32();
    p.itemClass = r.u32();
    p.subClass  = r.u32();
    p.name      = readCString(r);
    (void)readCString(r); (void)readCString(r); (void)readCString(r);  // name2/3/4 (empty)
    p.displayId     = r.u32();
    p.quality       = r.u32();
    p.flags         = r.u32();
    p.buyPrice      = r.u32();
    p.sellPrice     = r.u32();
    p.inventoryType = r.u32();
    p.allowableClass = r.u32();
    p.allowableRace  = r.u32();
    p.itemLevel     = r.u32();
    p.requiredLevel = r.u32();
    p.requiredSkill = r.u32();
    p.requiredSkillRank = r.u32();
    p.requiredSpell = r.u32();
    p.requiredHonorRank = r.u32();
    p.requiredCityRank  = r.u32();
    p.requiredReputationFaction = r.u32();
    p.requiredReputationRank    = r.u32();
    p.maxCount      = r.u32();
    p.stackable     = r.u32();
    p.containerSlots = r.u32();
    for (ItemStat& s : p.stats) {
        s.type  = r.u32();
        s.value = r.i32();
    }
    for (ItemDamage& d : p.damages) {
        d.min    = r.f32();
        d.max    = r.f32();
        d.school = r.u32();
    }
    p.armor     = r.u32();
    p.holyRes   = r.u32();
    p.fireRes   = r.u32();
    p.natureRes = r.u32();
    p.frostRes  = r.u32();
    p.shadowRes = r.u32();
    p.arcaneRes = r.u32();
    p.delay     = r.u32();
    p.ammoType  = r.u32();
    p.rangedModRange = r.f32();
    for (ItemSpellInfo& sp : p.spells) {
        sp.spellId          = r.u32();
        sp.trigger          = r.u32();
        sp.charges          = r.i32();
        sp.cooldown         = r.i32();
        sp.category         = r.u32();
        sp.categoryCooldown = r.i32();
    }
    p.bonding     = r.u32();
    p.description = readCString(r);
    p.pageText     = r.u32();
    p.languageId   = r.u32();
    p.pageMaterial = r.u32();
    p.startQuest   = r.u32();
    p.lockId       = r.u32();
    p.material     = r.u32();
    p.sheath       = r.u32();
    p.randomProperty = r.u32();
    p.block        = r.u32();
    p.itemSet      = r.u32();
    p.maxDurability = r.u32();
    p.area         = r.u32();
    p.map          = r.u32();
    p.bagFamily    = r.u32();
    return p;
}

// ---- "Item not found" reply -------------------------------------------------
// When the server has no template for the requested entry it answers with just
// a single u32: the entry with the high bit set. The client treats the high bit
// as a "does not exist" marker (and never caches it).
inline std::vector<uint8_t> encodeItemQueryNotFound(uint32_t entry) {
    ByteWriter w;
    w.u32(entry | 0x80000000u);
    return w.data();
}

}  // namespace wf
