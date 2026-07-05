#pragma once
// ---------------------------------------------------------------------------
// Quest template query codec for vanilla 1.12.1 (build 5875). When the client
// sees a quest id it hasn't cached (a quest-giver "!" , a quest-log entry), it
// asks the server for the full QuestTemplate and caches the reply in its
// DBCache. This reconstructs both halves of that exchange.
//
//   client -> CMSG_QUEST_QUERY           { u32 questId }
//   server -> SMSG_QUEST_QUERY_RESPONSE  { the QuestTemplate the client caches }
//
// Opcode values (vanilla 1.12.1, from mangos-zero Opcodes.h):
//   CMSG_QUEST_QUERY            = 0x05C
//   SMSG_QUEST_QUERY_RESPONSE   = 0x05D
//
// SMSG layout (verified vs mangos-zero PlayerMenu::SendQuestQueryResponse):
//   u32 questId
//   u32 method               (QuestMethod: 0/1/2 -- how the quest is completed)
//   u32 level                (quest level)
//   i32 zoneOrSort           (>0 zone to show under; <0 a QuestSort.dbc bucket)
//   u32 type                 (quest info type)
//   u32 repObjectiveFaction  (faction whose rep is a quest objective)
//   u32 repObjectiveValue
//   u32 reqOppositeRepFaction (always 0 in vanilla -- reserved wire slot)
//   u32 reqOppositeRepValue   (always 0 in vanilla -- reserved wire slot)
//   u32 nextQuestInChain
//   u32 rewOrReqMoney        (>0 money rewarded; server zeroes it if the quest
//                             has QUEST_FLAGS_HIDDEN_REWARDS)
//   u32 rewMoneyMaxLevel     (used by the client for XP-vs-money display)
//   u32 rewSpell             (spell whose icon is shown / cast on turn-in)
//   u32 srcItemId            (item auto-granted when the quest is accepted)
//   u32 questFlags
//   { u32 id; u32 count } x4  reward items       (QUEST_REWARDS_COUNT)
//   { u32 id; u32 count } x6  reward choice items (QUEST_REWARD_CHOICES_COUNT)
//   u32 pointMapId           (quest POI: map id)
//   f32 pointX
//   f32 pointY
//   u32 pointOpt
//   cstr title
//   cstr objectives          (the quest-log objective summary line)
//   cstr details             (the quest-giver "accept" body text)
//   cstr endText             (the "return to" completion blurb)
//   { u32 reqObjectId; u32 reqObjectCount; u32 reqItemId; u32 reqItemCount } x4
//                            (QUEST_OBJECTIVES_COUNT kill/gather objectives)
//   cstr objectiveText x4    (per-objective quest-log override lines)
//
// Vanilla-specific traits:
//   * Every numeric field is a bare u32/i32/f32 -- nothing is bit-packed or
//     length-prefixed. Strings are bare WoW "CString"s (raw bytes + NUL), NOT
//     length-prefixed lpstrings.
//   * All four counts are fixed at 4 objectives / 4 reward items / 6 reward
//     choices -- the slots are always emitted, empty ones as zeros.
//   * reqObjectId encodes a creature entry as its plain id, but a GAMEOBJECT
//     objective as (gameObjectId | 0x80000000) -- the high bit tags it as a GO.
//     We carry the already-encoded wire u32 so the round-trip is exact.
//   * TBC+ added a u32 SuggestedPlayers between `type` and `repObjectiveFaction`
//     and a trailing rewCasterSpell -- vanilla has NEITHER (noted in mangos with
//     the "[-ZERO]" comment).
//
// Pure over byte_reader/byte_writer -- no sockets, no opcode enums -- so the
// exchange round-trips deterministically (tests/test_quest_query.cpp). Cross-
// checked vs the GPL mangos-zero vanilla source as a fact reference; our own
// code, nothing copied.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"   // writeCString / readCString

namespace wf {

// Fixed slot counts (vanilla QuestDef.h).
inline constexpr int kQuestObjectivesCount    = 4;   // QUEST_OBJECTIVES_COUNT
inline constexpr int kQuestRewardsCount       = 4;   // QUEST_REWARDS_COUNT
inline constexpr int kQuestRewardChoicesCount = 6;   // QUEST_REWARD_CHOICES_COUNT

// ---- CMSG_QUEST_QUERY (client request) --------------------------------------
inline std::vector<uint8_t> encodeQuestQuery(uint32_t questId) {
    ByteWriter w;
    w.u32(questId);
    return w.data();
}
inline uint32_t decodeQuestQuery(ByteReader& r) {
    return r.u32();
}

// ---- SMSG_QUEST_QUERY_RESPONSE (server reply) -------------------------------
struct QuestQueryResponse {
    uint32_t questId               = 0;
    uint32_t method                = 0;
    uint32_t level                 = 0;
    int32_t  zoneOrSort            = 0;   // signed: <0 selects a QuestSort bucket
    uint32_t type                  = 0;
    uint32_t repObjectiveFaction   = 0;
    uint32_t repObjectiveValue     = 0;
    uint32_t reqOppositeRepFaction = 0;   // reserved (always 0 in vanilla)
    uint32_t reqOppositeRepValue   = 0;   // reserved (always 0 in vanilla)
    uint32_t nextQuestInChain      = 0;
    uint32_t rewOrReqMoney         = 0;   // reward money (or negative-as-u32 req)
    uint32_t rewMoneyMaxLevel      = 0;
    uint32_t rewSpell              = 0;
    uint32_t srcItemId             = 0;
    uint32_t questFlags            = 0;

    std::array<uint32_t, kQuestRewardsCount>       rewItemId{};
    std::array<uint32_t, kQuestRewardsCount>       rewItemCount{};
    std::array<uint32_t, kQuestRewardChoicesCount> rewChoiceItemId{};
    std::array<uint32_t, kQuestRewardChoicesCount> rewChoiceItemCount{};

    uint32_t pointMapId = 0;
    float    pointX     = 0.0f;
    float    pointY     = 0.0f;
    uint32_t pointOpt   = 0;

    std::string title;
    std::string objectives;   // quest-log objective summary
    std::string details;      // quest-giver body text
    std::string endText;      // completion blurb

    // Per-objective requirements. reqObjectId carries the WIRE value: a creature
    // entry as-is, or a gameobject id OR'd with 0x80000000.
    std::array<uint32_t, kQuestObjectivesCount> reqObjectId{};
    std::array<uint32_t, kQuestObjectivesCount> reqObjectCount{};
    std::array<uint32_t, kQuestObjectivesCount> reqItemId{};
    std::array<uint32_t, kQuestObjectivesCount> reqItemCount{};

    std::array<std::string, kQuestObjectivesCount> objectiveText{};
};

inline std::vector<uint8_t> encodeQuestQueryResponse(const QuestQueryResponse& q) {
    ByteWriter w;
    w.u32(q.questId);
    w.u32(q.method);
    w.u32(q.level);
    w.i32(q.zoneOrSort);
    w.u32(q.type);
    w.u32(q.repObjectiveFaction);
    w.u32(q.repObjectiveValue);
    w.u32(q.reqOppositeRepFaction);
    w.u32(q.reqOppositeRepValue);
    w.u32(q.nextQuestInChain);
    w.u32(q.rewOrReqMoney);
    w.u32(q.rewMoneyMaxLevel);
    w.u32(q.rewSpell);
    w.u32(q.srcItemId);
    w.u32(q.questFlags);

    for (int i = 0; i < kQuestRewardsCount; ++i) {
        w.u32(q.rewItemId[i]);
        w.u32(q.rewItemCount[i]);
    }
    for (int i = 0; i < kQuestRewardChoicesCount; ++i) {
        w.u32(q.rewChoiceItemId[i]);
        w.u32(q.rewChoiceItemCount[i]);
    }

    w.u32(q.pointMapId);
    w.f32(q.pointX);
    w.f32(q.pointY);
    w.u32(q.pointOpt);

    writeCString(w, q.title);
    writeCString(w, q.objectives);
    writeCString(w, q.details);
    writeCString(w, q.endText);

    for (int i = 0; i < kQuestObjectivesCount; ++i) {
        w.u32(q.reqObjectId[i]);
        w.u32(q.reqObjectCount[i]);
        w.u32(q.reqItemId[i]);
        w.u32(q.reqItemCount[i]);
    }
    for (int i = 0; i < kQuestObjectivesCount; ++i) {
        writeCString(w, q.objectiveText[i]);
    }
    return w.data();
}

inline QuestQueryResponse decodeQuestQueryResponse(ByteReader& r) {
    QuestQueryResponse q;
    q.questId               = r.u32();
    q.method                = r.u32();
    q.level                 = r.u32();
    q.zoneOrSort            = r.i32();
    q.type                  = r.u32();
    q.repObjectiveFaction   = r.u32();
    q.repObjectiveValue     = r.u32();
    q.reqOppositeRepFaction = r.u32();
    q.reqOppositeRepValue   = r.u32();
    q.nextQuestInChain      = r.u32();
    q.rewOrReqMoney         = r.u32();
    q.rewMoneyMaxLevel      = r.u32();
    q.rewSpell              = r.u32();
    q.srcItemId             = r.u32();
    q.questFlags            = r.u32();

    for (int i = 0; i < kQuestRewardsCount; ++i) {
        q.rewItemId[i]    = r.u32();
        q.rewItemCount[i] = r.u32();
    }
    for (int i = 0; i < kQuestRewardChoicesCount; ++i) {
        q.rewChoiceItemId[i]    = r.u32();
        q.rewChoiceItemCount[i] = r.u32();
    }

    q.pointMapId = r.u32();
    q.pointX     = r.f32();
    q.pointY     = r.f32();
    q.pointOpt   = r.u32();

    q.title      = readCString(r);
    q.objectives = readCString(r);
    q.details    = readCString(r);
    q.endText    = readCString(r);

    for (int i = 0; i < kQuestObjectivesCount; ++i) {
        q.reqObjectId[i]    = r.u32();
        q.reqObjectCount[i] = r.u32();
        q.reqItemId[i]      = r.u32();
        q.reqItemCount[i]   = r.u32();
    }
    for (int i = 0; i < kQuestObjectivesCount; ++i) {
        q.objectiveText[i] = readCString(r);
    }
    return q;
}

// GAMEOBJECT objectives ride the wire with the high bit set. These helpers make
// that explicit at call sites without changing the byte layout.
inline uint32_t questReqObjectFromGameObject(uint32_t gameObjectId) {
    return gameObjectId | 0x80000000u;
}
inline bool questReqObjectIsGameObject(uint32_t wireId) {
    return (wireId & 0x80000000u) != 0;
}

}  // namespace wf
