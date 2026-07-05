// ---------------------------------------------------------------------------
// Quest template query codec round-trip tests (src/net/quest_query.hpp). No
// sockets: the client request and the server QuestTemplate response are encoded
// and decoded back, asserting the exact vanilla 1.12.1 byte layout (fixed u32/
// i32/f32 numeric block, bare CString texts, 4 objective slots + 4/6 reward
// slots) survives intact.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/quest_query.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_quest_query() {
    std::printf("[net.quest_query]\n");

    // CMSG_QUEST_QUERY: just the u32 quest id.
    {
        std::vector<uint8_t> req = encodeQuestQuery(0x00001A2Bu);
        CHECK(req.size() == 4);
        ByteReader r(req.data(), req.size());
        CHECK(decodeQuestQuery(r) == 0x00001A2Bu);
        CHECK(r.remaining() == 0);
    }

    // SMSG_QUEST_QUERY_RESPONSE: fully populated template round-trips.
    {
        QuestQueryResponse in;
        in.questId               = 6961;
        in.method                = 2;
        in.level                 = 60;
        in.zoneOrSort            = -22;      // a QuestSort bucket (negative)
        in.type                  = 41;
        in.repObjectiveFaction   = 72;
        in.repObjectiveValue     = 21000;
        in.reqOppositeRepFaction = 0;
        in.reqOppositeRepValue   = 0;
        in.nextQuestInChain      = 6962;
        in.rewOrReqMoney         = 9500;
        in.rewMoneyMaxLevel      = 6100;
        in.rewSpell              = 12345;
        in.srcItemId             = 3000;
        in.questFlags            = 0x8;

        in.rewItemId       = { 811, 812, 0, 0 };
        in.rewItemCount    = { 1, 2, 0, 0 };
        in.rewChoiceItemId = { 900, 901, 902, 0, 0, 0 };
        in.rewChoiceItemCount = { 1, 1, 5, 0, 0, 0 };

        in.pointMapId = 1;
        in.pointX     = -1234.5f;
        in.pointY     = 678.25f;
        in.pointOpt   = 3;

        in.title      = "Kill Hogger";
        in.objectives = "Slay Hogger";
        in.details    = "Hogger threatens Elwynn Forest.";
        in.endText    = "Well done, hero.";

        // A creature objective (plain id) and a gameobject objective (high bit).
        in.reqObjectId    = { 448u, questReqObjectFromGameObject(1735u), 0u, 0u };
        in.reqObjectCount = { 1, 1, 0, 0 };
        in.reqItemId      = { 0, 0, 3421, 0 };
        in.reqItemCount   = { 0, 0, 4, 0 };
        in.objectiveText  = { "Hogger slain", "Chest looted", "", "" };

        std::vector<uint8_t> bytes = encodeQuestQueryResponse(in);
        ByteReader r(bytes.data(), bytes.size());
        QuestQueryResponse out = decodeQuestQueryResponse(r);
        CHECK(r.remaining() == 0);

        CHECK(out.questId == 6961 && out.method == 2 && out.level == 60);
        CHECK(out.zoneOrSort == -22 && out.type == 41);
        CHECK(out.repObjectiveFaction == 72 && out.repObjectiveValue == 21000);
        CHECK(out.reqOppositeRepFaction == 0 && out.reqOppositeRepValue == 0);
        CHECK(out.nextQuestInChain == 6962);
        CHECK(out.rewOrReqMoney == 9500 && out.rewMoneyMaxLevel == 6100);
        CHECK(out.rewSpell == 12345 && out.srcItemId == 3000 && out.questFlags == 0x8);

        CHECK(out.rewItemId[0] == 811 && out.rewItemCount[1] == 2);
        CHECK(out.rewChoiceItemId[2] == 902 && out.rewChoiceItemCount[2] == 5);

        CHECK(out.pointMapId == 1 && out.pointOpt == 3);
        CHECK_APPROX(out.pointX, -1234.5f);
        CHECK_APPROX(out.pointY, 678.25f);

        CHECK(out.title == "Kill Hogger");
        CHECK(out.objectives == "Slay Hogger");
        CHECK(out.details == "Hogger threatens Elwynn Forest.");
        CHECK(out.endText == "Well done, hero.");

        CHECK(out.reqObjectId[0] == 448u);
        CHECK(questReqObjectIsGameObject(out.reqObjectId[1]));
        CHECK(!questReqObjectIsGameObject(out.reqObjectId[0]));
        CHECK(out.reqObjectId[1] == (1735u | 0x80000000u));
        CHECK(out.reqItemId[2] == 3421 && out.reqItemCount[2] == 4);
        CHECK(out.objectiveText[0] == "Hogger slain");
        CHECK(out.objectiveText[1] == "Chest looted");
        CHECK(out.objectiveText[2].empty() && out.objectiveText[3].empty());
    }

    // Minimal all-zero template with every text empty: exact wire size is easy.
    // Fixed numeric block = 15 u32/i32 (60) + 4 reward pairs (32) + 6 choice
    // pairs (48) + point block u32+f32+f32+u32 (16) = 156. Then 4 empty CStrings
    // (4) + 4 objective quads of 4 u32 (64) + 4 empty CStrings (4) = 228.
    {
        QuestQueryResponse in;   // all fields default (zeros / empty strings)
        in.questId = 42;
        std::vector<uint8_t> bytes = encodeQuestQueryResponse(in);
        CHECK(bytes.size() == 156 + 4 + 64 + 4);   // == 228
        ByteReader r(bytes.data(), bytes.size());
        QuestQueryResponse out = decodeQuestQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.questId == 42);
        CHECK(out.title.empty() && out.objectives.empty() && out.endText.empty());
        CHECK(out.rewItemId[0] == 0 && out.reqObjectId[3] == 0);
        CHECK_APPROX(out.pointX, 0.0f);
    }
}
