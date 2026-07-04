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

    // ---- diagnostics ----------------------------------------------------
    size_t nameCount() const { return names_.size(); }
    size_t creatureCount() const { return creatures_.size(); }
    size_t gameObjectCount() const { return gameObjects_.size(); }
    size_t pendingCount() const {
        return namePending_.size() + creaturePending_.size() + goPending_.size();
    }
    void clear() {
        names_.clear(); creatures_.clear(); gameObjects_.clear();
        namePending_.clear(); creaturePending_.clear(); goPending_.clear();
    }

private:
    std::unordered_map<uint64_t, NameQueryResponse>       names_;
    std::unordered_map<uint32_t, CreatureQueryResponse>   creatures_;
    std::unordered_map<uint32_t, GameObjectQueryResponse> gameObjects_;
    std::unordered_set<uint64_t> namePending_;
    std::unordered_set<uint32_t> creaturePending_;
    std::unordered_set<uint32_t> goPending_;
};

}  // namespace wf
