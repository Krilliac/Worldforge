// ---------------------------------------------------------------------------
// Loot window codec round-trip tests (src/net/loot_response.hpp). No sockets:
// SMSG_LOOT_RESPONSE is encoded and decoded back, asserting the exact vanilla
// 1.12.1 byte layout (raw u64 guid, u8 lootType, u32 gold, u8 itemCount, then
// per slot { u8 index, u32 itemId, u32 count, u32 displayId, u32 randomSuffix,
// u32 randomProperty, u8 slotType }) survives, plus exact byte sizes.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/loot_response.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_loot_response() {
    std::printf("[net.loot_response]\n");

    // Header size: 8 (guid) + 1 (lootType) + 4 (gold) + 1 (itemCount).
    const size_t kHeader = 8 + 1 + 4 + 1;
    // Per-slot size: 1 (index) + 4*5 (itemId,count,displayId,randomSuffix,randomProperty) + 1 (slotType).
    const size_t kSlot = 1 + 4 + 4 + 4 + 4 + 4 + 1;

    // Empty loot window (already looted corpse): gold 0, no items.
    {
        LootResponse in;
        in.guid = 0x0000000100002ABCull;
        in.lootType = LOOT_CORPSE;
        in.gold = 0;

        std::vector<uint8_t> bytes = encodeLootResponse(in);
        CHECK(bytes.size() == kHeader);

        ByteReader r(bytes.data(), bytes.size());
        LootResponse out = decodeLootResponse(r);
        CHECK(out.guid == in.guid);
        CHECK(out.lootType == LOOT_CORPSE);
        CHECK(out.gold == 0);
        CHECK(out.items.empty());
        CHECK(r.remaining() == 0);
    }

    // Money-only loot (fishing): some copper, still no item slots.
    {
        LootResponse in;
        in.guid = 0x00000000DEADBEEFull;
        in.lootType = LOOT_FISHING;
        in.gold = 12345;

        std::vector<uint8_t> bytes = encodeLootResponse(in);
        CHECK(bytes.size() == kHeader);

        ByteReader r(bytes.data(), bytes.size());
        LootResponse out = decodeLootResponse(r);
        CHECK(out.guid == in.guid);
        CHECK(out.lootType == LOOT_FISHING);
        CHECK(out.gold == 12345);
        CHECK(out.items.empty());
        CHECK(r.remaining() == 0);
    }

    // Full loot with two item slots of differing slot types.
    {
        LootResponse in;
        in.guid = 0xF130000000A1B2C3ull;   // GameObject-high loot source guid
        in.lootType = LOOT_CORPSE;
        in.gold = 5000;

        LootItemView a;
        a.index = 0;
        a.itemId = 2589;        // Linen Cloth
        a.count = 3;
        a.displayId = 6866;
        a.randomSuffix = 0;
        a.randomProperty = 0;
        a.slotType = LOOT_SLOT_NORMAL;
        in.items.push_back(a);

        LootItemView b;
        b.index = 1;
        b.itemId = 18832;       // Brutality Blade (random-enchant capable)
        b.count = 1;
        b.displayId = 30606;
        b.randomSuffix = 47;
        b.randomProperty = 1805;
        b.slotType = LOOT_SLOT_MASTER;
        in.items.push_back(b);

        std::vector<uint8_t> bytes = encodeLootResponse(in);
        CHECK(bytes.size() == kHeader + 2 * kSlot);

        ByteReader r(bytes.data(), bytes.size());
        LootResponse out = decodeLootResponse(r);

        CHECK(out.guid == in.guid);
        CHECK(out.lootType == LOOT_CORPSE);
        CHECK(out.gold == 5000);
        CHECK(out.items.size() == 2);

        CHECK(out.items[0].index == 0);
        CHECK(out.items[0].itemId == 2589);
        CHECK(out.items[0].count == 3);
        CHECK(out.items[0].displayId == 6866);
        CHECK(out.items[0].randomSuffix == 0);
        CHECK(out.items[0].randomProperty == 0);
        CHECK(out.items[0].slotType == LOOT_SLOT_NORMAL);

        CHECK(out.items[1].index == 1);
        CHECK(out.items[1].itemId == 18832);
        CHECK(out.items[1].count == 1);
        CHECK(out.items[1].displayId == 30606);
        CHECK(out.items[1].randomSuffix == 47);
        CHECK(out.items[1].randomProperty == 1805);
        CHECK(out.items[1].slotType == LOOT_SLOT_MASTER);

        CHECK(r.remaining() == 0);
    }
}
