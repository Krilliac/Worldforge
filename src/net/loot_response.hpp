#pragma once
// ---------------------------------------------------------------------------
// Loot window codec for vanilla 1.12.1 (build 5875). SMSG_LOOT_RESPONSE is what
// the server sends after the client CMSG_LOOT a corpse / chest / node: it fills
// the loot frame with the available money and item slots. This reconstructs the
// server->client form (what the client renders as the loot roll list).
//
//   u64 guid            (the loot source ObjectGuid -- raw u64, NOT packed)
//   u8  lootType        (LootType: CORPSE/PICKPOCKETING/FISHING/DISENCHANTING)
//   u32 gold            (copper amount; 0 when already looted / none)
//   u8  itemCount       (number of item slots that follow)
//   -- itemCount x LootItemView --
//     u8  index         (slot index the client echoes back on CMSG_AUTOSTORE_LOOT_ITEM)
//     u32 itemId        (Item.dbc entry)
//     u32 count         (stack size in this slot)
//     u32 displayId     (ItemDisplayInfo.dbc id, from the item prototype)
//     u32 randomSuffix  (item random-property suffix factor; 0 for normal items)
//     u32 randomProperty(ItemRandomProperties.dbc id; 0 for normal items)
//     u8  slotType      (LootSlotType: 0 normal / 1 view-only / 2 master / 3 reqs)
//
// Vanilla-specific traits (verified vs mangos-zero LootMgr LootView/LootItem
// operator<<): the source guid is a RAW u64 (mangos `data << ObjectGuid(guid)`
// serialises GetRawValue(), no pack byte). The item count is a single u8 -- there
// is no u32 gold-then-u32-count widening that later notation sometimes implies.
// Each slot carries BOTH a randomSuffix u32 (mangos writes a literal `uint32(0)`
// here) AND a randomProperty u32, in that order, before the trailing slotType u8.
// gold precedes the item count. When permission is NONE the server still emits
// gold(=0) + count(=0) and nothing else -- an empty loot window is a valid packet.
//
// Pure over byte_reader/byte_writer -- no sockets -- so it round-trips
// deterministically (tests/test_loot_response.cpp). Cross-checked vs the GPL
// mangos-zero vanilla source used as a fact reference; our own code, no copy.
//
// Opcode value (for the framing layer, kept out of this pure body codec):
//   SMSG_LOOT_RESPONSE = 0x0160
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"    // readCString / writeCString helpers (reused, not redefined)

namespace wf {

// LootType values (vanilla 1.12.1). The wire byte the server actually sends after
// remapping the client-unsupported types (skinning->pickpocketing, etc.).
enum LootType : uint8_t {
    LOOT_CORPSE          = 1,
    LOOT_PICKPOCKETING   = 2,
    LOOT_FISHING         = 3,
    LOOT_DISENCHANTING   = 4,
};

// LootSlotType values (vanilla 1.12.1) -- how the client is allowed to interact
// with a given slot.
enum LootSlotType : uint8_t {
    LOOT_SLOT_NORMAL = 0,   // can be looted
    LOOT_SLOT_VIEW   = 1,   // view only (loot attempts ignored)
    LOOT_SLOT_MASTER = 2,   // master looter selection only
    LOOT_SLOT_REQS   = 3,   // cannot be looted (missing requirements)
};

// ---- one loot slot ----------------------------------------------------------
struct LootItemView {
    uint8_t  index          = 0;   // slot index echoed back by the client
    uint32_t itemId         = 0;   // Item.dbc entry
    uint32_t count          = 0;   // stack size in this slot
    uint32_t displayId      = 0;   // ItemDisplayInfo.dbc id
    uint32_t randomSuffix   = 0;   // random-property suffix factor (0 = normal)
    uint32_t randomProperty = 0;   // ItemRandomProperties.dbc id (0 = normal)
    uint8_t  slotType       = LOOT_SLOT_NORMAL;
};

// ---- SMSG_LOOT_RESPONSE (server -> client) ----------------------------------
struct LootResponse {
    uint64_t                  guid     = 0;   // loot source ObjectGuid (raw u64)
    uint8_t                   lootType = LOOT_CORPSE;
    uint32_t                  gold     = 0;   // copper
    std::vector<LootItemView> items;
};

inline std::vector<uint8_t> encodeLootResponse(const LootResponse& lr) {
    ByteWriter w;
    w.u64(lr.guid);
    w.u8(lr.lootType);
    w.u32(lr.gold);
    w.u8(static_cast<uint8_t>(lr.items.size()));   // item count (u8 in vanilla)
    for (const LootItemView& it : lr.items) {
        w.u8(it.index);
        w.u32(it.itemId);
        w.u32(it.count);
        w.u32(it.displayId);
        w.u32(it.randomSuffix);
        w.u32(it.randomProperty);
        w.u8(it.slotType);
    }
    return w.data();
}

inline LootResponse decodeLootResponse(ByteReader& r) {
    LootResponse lr;
    lr.guid     = r.u64();
    lr.lootType = r.u8();
    lr.gold     = r.u32();
    uint8_t n   = r.u8();
    lr.items.reserve(n);
    for (uint8_t i = 0; i < n; ++i) {
        LootItemView it;
        it.index          = r.u8();
        it.itemId         = r.u32();
        it.count          = r.u32();
        it.displayId      = r.u32();
        it.randomSuffix   = r.u32();
        it.randomProperty = r.u32();
        it.slotType       = r.u8();
        lr.items.push_back(it);
    }
    return lr;
}

}  // namespace wf
