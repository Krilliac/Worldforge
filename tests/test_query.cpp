// ---------------------------------------------------------------------------
// Entity name-query codec round-trip tests (src/net/query.hpp). No sockets: the
// client request and server response are encoded and decoded back, asserting the
// exact vanilla 1.12.1 byte layout (u64 guid, cstring name, empty realm cstring,
// u32 race/gender/class) survives.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/query.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_query() {
    std::printf("[net.query]\n");

    // CMSG_NAME_QUERY: just the u64 guid.
    {
        std::vector<uint8_t> req = encodeNameQuery(0x00F1000000001234ull);
        CHECK(req.size() == 8);
        ByteReader r(req.data(), req.size());
        CHECK(decodeNameQuery(r) == 0x00F1000000001234ull);
        CHECK(r.remaining() == 0);
    }

    // SMSG_NAME_QUERY_RESPONSE round-trip.
    {
        NameQueryResponse in;
        in.guid = 0x0000000000ABCDEFull;
        in.name = "Thrall";
        in.race = 2;      // Orc
        in.gender = 0;    // male
        in.cls = 7;       // Shaman

        std::vector<uint8_t> bytes = encodeNameQueryResponse(in);
        // 8 (guid) + 7 ("Thrall\0") + 1 (empty realm cstring) + 12 (3xu32) = 28.
        CHECK(bytes.size() == 8 + 7 + 1 + 12);

        ByteReader r(bytes.data(), bytes.size());
        NameQueryResponse out = decodeNameQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.guid == in.guid);
        CHECK(out.name == "Thrall");
        CHECK(out.race == 2 && out.gender == 0 && out.cls == 7);
    }

    // Empty name still terminates cleanly (guid resolved, name blank).
    {
        NameQueryResponse in; in.guid = 5; in.name = ""; in.race = 1; in.cls = 1;
        std::vector<uint8_t> b = encodeNameQueryResponse(in);
        ByteReader r(b.data(), b.size());
        NameQueryResponse out = decodeNameQueryResponse(r);
        CHECK(out.name.empty() && out.guid == 5 && out.race == 1 && r.remaining() == 0);
    }

    // Opcode values match the vanilla table.
    CHECK(CMSG_NAME_QUERY == 0x050);
    CHECK(SMSG_NAME_QUERY_RESPONSE == 0x051);

    // CString helpers: write/read parity incl. embedded-free content.
    {
        ByteWriter w; writeCString(w, "Azeroth");
        CHECK(w.data().size() == 8);             // 7 chars + NUL
        ByteReader r(w.data().data(), w.data().size());
        CHECK(readCString(r) == "Azeroth" && r.remaining() == 0);
    }
}
