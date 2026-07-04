// ---------------------------------------------------------------------------
// QueryCache tests (src/net/query_cache.hpp): the client dynamic-record cache.
// Drives the miss -> query -> response -> cached lifecycle with no sockets.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/query_cache.hpp"
#include "net/query.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_query_cache() {
    std::printf("[net.query_cache]\n");

    QueryCache cache;

    // --- name: miss fires exactly one query, then caches the response -------
    const uint64_t g = 0x00F1000000001234ull;
    CHECK(cache.name(g) == nullptr);                 // unresolved
    std::vector<uint8_t> q = cache.buildNameQuery(g);
    CHECK(!q.empty());                               // first miss -> query bytes
    CHECK(cache.pendingCount() == 1);
    // A repeat while in flight must NOT re-query (no duplicate spam).
    CHECK(cache.buildNameQuery(g).empty());
    CHECK(!cache.wantName(g));

    // The query bytes are a real CMSG_NAME_QUERY (round-trips to the guid).
    { ByteReader r(q.data(), q.size()); CHECK(decodeNameQuery(r) == g); }

    // Server answers -> cache it, clear the in-flight mark.
    NameQueryResponse nr; nr.guid = g; nr.name = "Arthas"; nr.race = 1; nr.cls = 2;
    cache.onNameResponse(nr);
    CHECK(cache.pendingCount() == 0);
    const NameQueryResponse* got = cache.name(g);
    CHECK(got && got->name == "Arthas" && got->cls == 2);
    CHECK(cache.nameCount() == 1);
    // Now resolved -> no further query wanted.
    CHECK(cache.buildNameQuery(g).empty());

    // --- creature: same lifecycle, keyed by entry ---------------------------
    const uint32_t entry = 299;
    const uint64_t cguid = 0xF13000000000AAAAull;
    CHECK(cache.creature(entry) == nullptr);
    std::vector<uint8_t> cq = cache.buildCreatureQuery(entry, cguid);
    CHECK(!cq.empty());
    { ByteReader r(cq.data(), cq.size()); CreatureQueryRequest req = decodeCreatureQuery(r);
      CHECK(req.entry == entry && req.guid == cguid); }
    CHECK(cache.buildCreatureQuery(entry, cguid).empty());   // in flight -> no dup

    CreatureQueryResponse cr; cr.entry = entry; cr.name = "Kobold"; cr.displayId = 10;
    cache.onCreatureResponse(cr);
    CHECK(cache.creature(entry) && cache.creature(entry)->name == "Kobold");
    CHECK(cache.creatureCount() == 1 && cache.pendingCount() == 0);

    // Distinct keys are independent; clear() empties everything.
    CHECK(cache.name(g + 1) == nullptr && cache.creature(entry + 1) == nullptr);
    cache.clear();
    CHECK(cache.nameCount() == 0 && cache.creatureCount() == 0);
    CHECK(!cache.buildNameQuery(g).empty());         // after clear it re-queries
}
