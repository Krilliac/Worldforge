// ---------------------------------------------------------------------------
// Guild query codec round-trip tests (src/net/guild_query.hpp). No sockets: the
// client request and server response are encoded and decoded back, asserting the
// exact vanilla 1.12.1 byte layout (u32 guildId, cstring name, ten cstring rank
// names padded with empty slots, five u32 emblem fields) survives.
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/guild_query.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_guild_query() {
    std::printf("[net.guild_query]\n");

    // CMSG_GUILD_QUERY: just the u32 guild id.
    {
        std::vector<uint8_t> req = encodeGuildQuery(0x0000BEEFu);
        CHECK(req.size() == 4);
        ByteReader r(req.data(), req.size());
        CHECK(decodeGuildQuery(r) == 0x0000BEEFu);
        CHECK(r.remaining() == 0);
    }

    // SMSG_GUILD_QUERY_RESPONSE round-trip: a fully-populated guild.
    {
        GuildQueryResponse in;
        in.guildId = 42;
        in.name = "Knights of the Silver Hand";
        in.rankNames[0] = "Guild Master";
        in.rankNames[1] = "Officer";
        in.rankNames[2] = "Veteran";
        in.rankNames[3] = "Member";
        in.rankNames[4] = "Initiate";
        // ranks 5..9 left empty (unused slots still transmitted)
        in.emblemStyle = 4;
        in.emblemColor = 9;
        in.borderStyle = 1;
        in.borderColor = 2;
        in.background  = 7;

        std::vector<uint8_t> b = encodeGuildQueryResponse(in);

        // 4 (guildId) + name(26+1) + 5 named ranks + 5 empty ranks + 20 (5*u32).
        size_t expected = 4;
        expected += in.name.size() + 1;
        for (size_t i = 0; i < GUILD_RANKS_MAX_COUNT; ++i)
            expected += in.rankNames[i].size() + 1;   // empty slot == 1 NUL byte
        expected += 5 * 4;
        CHECK(b.size() == expected);

        ByteReader r(b.data(), b.size());
        GuildQueryResponse out = decodeGuildQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.guildId == 42);
        CHECK(out.name == "Knights of the Silver Hand");
        CHECK(out.rankNames[0] == "Guild Master");
        CHECK(out.rankNames[4] == "Initiate");
        CHECK(out.rankNames[5].empty());
        CHECK(out.rankNames[9].empty());
        CHECK(out.emblemStyle == 4 && out.emblemColor == 9);
        CHECK(out.borderStyle == 1 && out.borderColor == 2 && out.background == 7);
    }

    // Minimal guild: empty name, all ten rank slots empty. The wire still carries
    // 11 lone NUL bytes (name + 10 ranks) between the ids and the emblem block.
    {
        GuildQueryResponse in;
        in.guildId = 1;
        // name empty, every rank empty
        std::vector<uint8_t> b = encodeGuildQueryResponse(in);
        // 4 + 1 (name NUL) + 10 (rank NULs) + 20 (5*u32) = 35.
        CHECK(b.size() == 4 + 1 + GUILD_RANKS_MAX_COUNT + 20);

        ByteReader r(b.data(), b.size());
        GuildQueryResponse out = decodeGuildQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.guildId == 1);
        CHECK(out.name.empty());
        CHECK(out.rankNames[0].empty() && out.rankNames[9].empty());
        CHECK(out.emblemStyle == 0 && out.background == 0);
    }

    // The exchange must round-trip regardless of which slots are named.
    {
        GuildQueryResponse in;
        in.guildId = 0x00ABCDEFu;
        in.name = "Lonewolf";
        in.rankNames[9] = "Trial";      // only the last slot named
        in.emblemStyle = 65; in.emblemColor = 0; in.borderStyle = 3;
        in.borderColor = 4; in.background = 11;

        std::vector<uint8_t> b = encodeGuildQueryResponse(in);
        ByteReader r(b.data(), b.size());
        GuildQueryResponse out = decodeGuildQueryResponse(r);
        CHECK(r.remaining() == 0);
        CHECK(out.guildId == 0x00ABCDEFu);
        CHECK(out.name == "Lonewolf");
        CHECK(out.rankNames[0].empty() && out.rankNames[9] == "Trial");
        CHECK(out.emblemStyle == 65 && out.borderStyle == 3 && out.background == 11);
    }
}
