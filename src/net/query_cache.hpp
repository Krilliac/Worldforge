#pragma once
// ---------------------------------------------------------------------------
// QueryCache: the client-side dynamic-record cache for vanilla 1.12.1 -- the
// alpha's DBClient "DBCache<T>" (server-pushed records keyed by id), as opposed
// to WowClientDB<T> (static DBC files). The client resolves a GUID's player name
// or a creature entry's template lazily: on a miss it fires a query
// (net/query.hpp), remembers the request is in flight so it doesn't spam
// duplicates, and caches the response for reuse (nameplates, chat, tooltips).
//
// This is pure state + policy over the query codecs -- no sockets. The owner
// pumps it:  if (cache.wantName(guid)) socket.send(cache.buildNameQuery(guid));
// then on SMSG_NAME_QUERY_RESPONSE:  cache.onNameResponse(decode...(r));
// and reads back:  if (auto* n = cache.name(guid)) label = n->name;
// ---------------------------------------------------------------------------
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "net/query.hpp"
#include "net/item_query.hpp"
#include "net/quest_query.hpp"
#include "net/guild_query.hpp"

namespace wf {

class QueryCache {
public:
    // ---- player name cache (by guid) ------------------------------------
    // Cached response, or nullptr if not resolved yet.
    const NameQueryResponse* name(uint64_t guid) const {
        auto it = names_.find(guid);
        return it == names_.end() ? nullptr : &it->second;
    }
    // True if this guid is neither cached nor already queried -- i.e. the caller
    // should send a name query now. Marks it in-flight so repeat frames don't
    // re-ask. (Prefer buildNameQuery(), which does this + returns the bytes.)
    bool wantName(uint64_t guid) {
        if (names_.count(guid) || namePending_.count(guid)) return false;
        namePending_.insert(guid);
        return true;
    }
    // If the name is unresolved and not in flight, mark it in flight and return
    // the CMSG_NAME_QUERY bytes to send; otherwise return an empty vector.
    std::vector<uint8_t> buildNameQuery(uint64_t guid) {
        if (!wantName(guid)) return {};
        return encodeNameQuery(guid);
    }
    // Ingest a SMSG_NAME_QUERY_RESPONSE: cache it, clear the in-flight mark.
    void onNameResponse(const NameQueryResponse& r) {
        namePending_.erase(r.guid);
        names_[r.guid] = r;
    }

    // ---- creature template cache (by entry) -----------------------------
    const CreatureQueryResponse* creature(uint32_t entry) const {
        auto it = creatures_.find(entry);
        return it == creatures_.end() ? nullptr : &it->second;
    }
    bool wantCreature(uint32_t entry) {
        if (creatures_.count(entry) || creaturePending_.count(entry)) return false;
        creaturePending_.insert(entry);
        return true;
    }
    // The client passes the instance guid it saw so the server can pick a live
    // displayId; the cache keys on entry (the template).
    std::vector<uint8_t> buildCreatureQuery(uint32_t entry, uint64_t guid) {
        if (!wantCreature(entry)) return {};
        return encodeCreatureQuery(entry, guid);
    }
    void onCreatureResponse(const CreatureQueryResponse& r) {
        creaturePending_.erase(r.entry);
        creatures_[r.entry] = r;
    }

    // ---- gameobject template cache (by entry) ---------------------------
    const GameObjectQueryResponse* gameObject(uint32_t entry) const {
        auto it = gameObjects_.find(entry);
        return it == gameObjects_.end() ? nullptr : &it->second;
    }
    bool wantGameObject(uint32_t entry) {
        if (gameObjects_.count(entry) || goPending_.count(entry)) return false;
        goPending_.insert(entry);
        return true;
    }
    std::vector<uint8_t> buildGameObjectQuery(uint32_t entry, uint64_t guid) {
        if (!wantGameObject(entry)) return {};
        return encodeGameObjectQuery(entry, guid);
    }
    void onGameObjectResponse(const GameObjectQueryResponse& r) {
        goPending_.erase(r.entry);
        gameObjects_[r.entry] = r;
    }

    // ---- item template cache (by entry) ---------------------------------
    const ItemQueryResponse* item(uint32_t entry) const {
        auto it = items_.find(entry);
        return it == items_.end() ? nullptr : &it->second;
    }
    bool wantItem(uint32_t entry) {
        if (items_.count(entry) || itemPending_.count(entry)) return false;
        itemPending_.insert(entry);
        return true;
    }
    std::vector<uint8_t> buildItemQuery(uint32_t entry, uint64_t guid) {
        if (!wantItem(entry)) return {};
        return encodeItemQuery(entry, guid);
    }
    void onItemResponse(const ItemQueryResponse& r) {
        itemPending_.erase(r.entry);
        items_[r.entry] = r;
    }

    // ---- quest template cache (by quest id) -----------------------------
    const QuestQueryResponse* quest(uint32_t questId) const {
        auto it = quests_.find(questId);
        return it == quests_.end() ? nullptr : &it->second;
    }
    bool wantQuest(uint32_t questId) {
        if (quests_.count(questId) || questPending_.count(questId)) return false;
        questPending_.insert(questId);
        return true;
    }
    std::vector<uint8_t> buildQuestQuery(uint32_t questId) {
        if (!wantQuest(questId)) return {};
        return encodeQuestQuery(questId);
    }
    void onQuestResponse(const QuestQueryResponse& r) {
        questPending_.erase(r.questId);
        quests_[r.questId] = r;
    }

    // ---- guild cache (by guild id) --------------------------------------
    const GuildQueryResponse* guild(uint32_t guildId) const {
        auto it = guilds_.find(guildId);
        return it == guilds_.end() ? nullptr : &it->second;
    }
    bool wantGuild(uint32_t guildId) {
        if (guilds_.count(guildId) || guildPending_.count(guildId)) return false;
        guildPending_.insert(guildId);
        return true;
    }
    std::vector<uint8_t> buildGuildQuery(uint32_t guildId) {
        if (!wantGuild(guildId)) return {};
        return encodeGuildQuery(guildId);
    }
    void onGuildResponse(const GuildQueryResponse& r) {
        guildPending_.erase(r.guildId);
        guilds_[r.guildId] = r;
    }

    // ---- diagnostics ----------------------------------------------------
    size_t nameCount() const { return names_.size(); }
    size_t creatureCount() const { return creatures_.size(); }
    size_t gameObjectCount() const { return gameObjects_.size(); }
    size_t itemCount() const { return items_.size(); }
    size_t questCount() const { return quests_.size(); }
    size_t guildCount() const { return guilds_.size(); }
    size_t pendingCount() const {
        return namePending_.size() + creaturePending_.size() + goPending_.size()
             + itemPending_.size() + questPending_.size() + guildPending_.size();
    }
    void clear() {
        names_.clear(); creatures_.clear(); gameObjects_.clear();
        items_.clear(); quests_.clear(); guilds_.clear();
        namePending_.clear(); creaturePending_.clear(); goPending_.clear();
        itemPending_.clear(); questPending_.clear(); guildPending_.clear();
    }

private:
    std::unordered_map<uint64_t, NameQueryResponse>       names_;
    std::unordered_map<uint32_t, CreatureQueryResponse>   creatures_;
    std::unordered_map<uint32_t, GameObjectQueryResponse> gameObjects_;
    std::unordered_map<uint32_t, ItemQueryResponse>       items_;
    std::unordered_map<uint32_t, QuestQueryResponse>      quests_;
    std::unordered_map<uint32_t, GuildQueryResponse>      guilds_;
    std::unordered_set<uint64_t> namePending_;
    std::unordered_set<uint32_t> creaturePending_;
    std::unordered_set<uint32_t> goPending_;
    std::unordered_set<uint32_t> itemPending_;
    std::unordered_set<uint32_t> questPending_;
    std::unordered_set<uint32_t> guildPending_;
};

}  // namespace wf
