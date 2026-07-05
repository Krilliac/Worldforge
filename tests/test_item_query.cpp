// ---------------------------------------------------------------------------
// Item query codec round-trip tests (src/net/item_query.hpp). No sockets: the
// client request and the full ItemTemplate response are encoded and decoded
// back, asserting the exact vanilla 1.12.1 byte layout survives -- all-u32
// scalar fields, three empty name slots, bare CString name/description, and the
// stats[10]/damages[5]/spells[5] arrays.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/item_query.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_item_query() {
    std::printf("[net.item_query]\n");

    // CMSG_ITEM_QUERY_SINGLE: u32 entry + raw u64 guid.
    {
        std::vector<uint8_t> req = encodeItemQuery(19019, 0x4000000000012345ull);
        CHECK(req.size() == 4 + 8);
        ByteReader r(req.data(), req.size());
        ItemQueryRequest q = decodeItemQuery(r);
        CHECK(q.entry == 19019 && q.guid == 0x4000000000012345ull);
        CHECK(r.remaining() == 0);
    }

    // SMSG_ITEM_QUERY_SINGLE_RESPONSE full round-trip.
    {
        ItemQueryResponse in;
        in.entry = 19019;                 // Thunderfury
        in.itemClass = 2;                 // weapon
        in.subClass = 7;                  // sword (one-hand)
        in.name = "Thunderfury, Blessed Blade of the Windseeker";
        in.displayId = 30606;
        in.quality = 5;                   // legendary
        in.flags = 0;
        in.buyPrice = 0;
        in.sellPrice = 143010;
        in.inventoryType = 13;            // weapon
        in.allowableClass = 0xFFFFFFFFu;  // all classes
        in.allowableRace = 0xFFFFFFFFu;   // all races
        in.itemLevel = 80;
        in.requiredLevel = 60;
        in.requiredSkill = 43;
        in.requiredSkillRank = 1;
        in.requiredSpell = 0;
        in.requiredHonorRank = 0;
        in.requiredCityRank = 0;
        in.requiredReputationFaction = 0;
        in.requiredReputationRank = 0;
        in.maxCount = 0;
        in.stackable = 1;
        in.containerSlots = 0;
        in.stats[0] = { 3, 4 };           // +4 Agility
        in.stats[1] = { 7, 6 };           // +6 Stamina
        in.damages[0] = { 16.0f, 30.0f, 0 };
        in.damages[1] = { 0.0f, 0.0f, 0 };
        in.armor = 0;
        in.holyRes = 0; in.fireRes = 0; in.natureRes = 8;
        in.frostRes = 0; in.shadowRes = 0; in.arcaneRes = 0;
        in.delay = 1900;
        in.ammoType = 0;
        in.rangedModRange = 0.0f;
        in.spells[0] = { 21992, 1, -1, 0, 0, -1 };   // trigger-on-hit proc
        in.bonding = 1;                   // bind on pickup
        in.description = "Left hand of the Windseeker.";
        in.pageText = 0;
        in.languageId = 0;
        in.pageMaterial = 0;
        in.startQuest = 0;
        in.lockId = 0;
        in.material = 1;
        in.sheath = 3;
        in.randomProperty = 0;
        in.block = 0;
        in.itemSet = 0;
        in.maxDurability = 125;
        in.area = 0;
        in.map = 0;
        in.bagFamily = 0;

        std::vector<uint8_t> bytes = encodeItemQueryResponse(in);

        // Fixed portion (everything except the two variable CStrings) is 455
        // bytes: 3 u32 head (12) + 3 empty-name bytes (3) + 20 u32 block (80) +
        // stats 10*(4+4)=80 + damages 5*(4+4+4)=60 + 7 resist u32 (28) +
        // delay/ammo/rangedRange (12) + spells 5*(6*4)=120 + bonding (4) +
        // 14 trailing u32 (56).  Plus name+NUL and description+NUL.
        const size_t fixed = 455;
        CHECK(bytes.size() == fixed + (in.name.size() + 1) + (in.description.size() + 1));

        ByteReader r(bytes.data(), bytes.size());
        ItemQueryResponse out = decodeItemQueryResponse(r);
        CHECK(r.remaining() == 0);

        CHECK(out.entry == in.entry);
        CHECK(out.itemClass == 2 && out.subClass == 7);
        CHECK(out.name == in.name);
        CHECK(out.displayId == 30606 && out.quality == 5);
        CHECK(out.sellPrice == 143010 && out.inventoryType == 13);
        CHECK(out.allowableClass == 0xFFFFFFFFu && out.allowableRace == 0xFFFFFFFFu);
        CHECK(out.itemLevel == 80 && out.requiredLevel == 60);
        CHECK(out.requiredSkill == 43 && out.requiredSkillRank == 1);
        CHECK(out.stackable == 1);
        CHECK(out.stats[0].type == 3 && out.stats[0].value == 4);
        CHECK(out.stats[1].type == 7 && out.stats[1].value == 6);
        CHECK_APPROX(out.damages[0].min, 16.0f);
        CHECK_APPROX(out.damages[0].max, 30.0f);
        CHECK(out.natureRes == 8);
        CHECK(out.delay == 1900);
        CHECK(out.spells[0].spellId == 21992 && out.spells[0].trigger == 1);
        CHECK(out.spells[0].charges == -1 && out.spells[0].categoryCooldown == -1);
        CHECK(out.bonding == 1);
        CHECK(out.description == in.description);
        CHECK(out.maxDurability == 125 && out.sheath == 3 && out.material == 1);
        CHECK(out.map == 0 && out.bagFamily == 0);
    }

    // Minimal item: empty name + empty description still terminate cleanly and
    // the byte size is exactly the fixed portion plus two lone NUL terminators.
    {
        ItemQueryResponse in;
        in.entry = 6948;                  // Hearthstone
        in.itemClass = 15;                // miscellaneous
        in.name = "";
        in.description = "";
        in.quality = 1;
        in.maxCount = 1;
        in.stackable = 1;

        std::vector<uint8_t> bytes = encodeItemQueryResponse(in);
        CHECK(bytes.size() == 455 + 1 + 1);

        ByteReader r(bytes.data(), bytes.size());
        ItemQueryResponse out = decodeItemQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.entry == 6948 && out.name.empty() && out.description.empty());
        CHECK(out.itemClass == 15 && out.quality == 1 && out.stackable == 1);
    }

    // Negative / signed wire values survive as-is (charges, category cooldown,
    // and a signed stat value).
    {
        ItemQueryResponse in;
        in.entry = 1;
        in.name = "Test";
        in.stats[0] = { 5, -10 };         // Intellect penalty (signed value)
        in.spells[0] = { 100, 2, -3, 15000, 4, -1 };
        std::vector<uint8_t> bytes = encodeItemQueryResponse(in);
        ByteReader r(bytes.data(), bytes.size());
        ItemQueryResponse out = decodeItemQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.stats[0].value == -10);
        CHECK(out.spells[0].charges == -3 && out.spells[0].cooldown == 15000);
        CHECK(out.spells[0].categoryCooldown == -1);
    }

    // "Item not found" reply: a single u32 == entry with the high bit set.
    {
        std::vector<uint8_t> b = encodeItemQueryNotFound(19019);
        CHECK(b.size() == 4);
        ByteReader r(b.data(), b.size());
        CHECK(r.u32() == (19019u | 0x80000000u));
        CHECK(r.remaining() == 0);
    }
}
