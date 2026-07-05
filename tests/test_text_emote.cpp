// ---------------------------------------------------------------------------
// Text-emote codec round-trip tests (src/net/text_emote.hpp). No sockets: the
// client request (CMSG) and server broadcast (SMSG) are encoded then decoded
// back, asserting the exact vanilla 1.12.1 byte layout survives -- u32/u32/u64
// on the CMSG side (guid last), and u64/u32/u32/u32-len + NUL-terminated name
// on the SMSG side (empty target still emits a length-1 empty cstring).
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/text_emote.hpp"
#include "byte_reader.hpp"

using namespace wf;

void test_text_emote() {
    std::printf("[net.text_emote]\n");

    // CMSG_TEXT_EMOTE: u32 textEmote, u32 emoteNum, u64 targetGuid (guid last).
    {
        TextEmoteRequest in;
        in.textEmote  = 10;   // TEXT_EMOTE_WAVE
        in.emoteNum   = 0;
        in.targetGuid = 0x00F1000000005678ull;

        std::vector<uint8_t> bytes = encodeTextEmoteRequest(in);
        CHECK(bytes.size() == 4 + 4 + 8);   // 16 bytes fixed

        ByteReader r(bytes.data(), bytes.size());
        TextEmoteRequest out = decodeTextEmoteRequest(r);
        CHECK(r.remaining() == 0);
        CHECK(out.textEmote == 10);
        CHECK(out.emoteNum == 0);
        CHECK(out.targetGuid == 0x00F1000000005678ull);
    }

    // Untargeted emote (guid 0) still round-trips at the same fixed size.
    {
        TextEmoteRequest in; in.textEmote = 34; in.emoteNum = 2; in.targetGuid = 0;
        std::vector<uint8_t> bytes = encodeTextEmoteRequest(in);
        CHECK(bytes.size() == 16);
        ByteReader r(bytes.data(), bytes.size());
        TextEmoteRequest out = decodeTextEmoteRequest(r);
        CHECK(out.textEmote == 34 && out.emoteNum == 2 && out.targetGuid == 0);
        CHECK(r.remaining() == 0);
    }

    // SMSG_TEXT_EMOTE with a named target: u64 guid, u32 emote, u32 num,
    // u32 nameLen(=strlen+1), then the NUL-terminated name.
    {
        TextEmoteNotification in;
        in.senderGuid = 0x0000000000ABCDEFull;
        in.textEmote  = 10;
        in.emoteNum   = 0;
        in.targetName = "Grommash";

        std::vector<uint8_t> bytes = encodeTextEmoteNotification(in);
        // 8(guid) + 4(emote) + 4(num) + 4(nameLen) + 9("Grommash\0") = 29.
        CHECK(bytes.size() == 8 + 4 + 4 + 4 + 9);

        ByteReader r(bytes.data(), bytes.size());
        TextEmoteNotification out = decodeTextEmoteNotification(r);
        CHECK(r.remaining() == 0);
        CHECK(out.senderGuid == in.senderGuid);
        CHECK(out.textEmote == 10 && out.emoteNum == 0);
        CHECK(out.targetName == "Grommash");
    }

    // SMSG_TEXT_EMOTE with NO target: nameLen == 1, a single NUL byte follows.
    {
        TextEmoteNotification in;
        in.senderGuid = 42;
        in.textEmote  = 34;   // TEXT_EMOTE_DANCE
        in.emoteNum   = 5;
        in.targetName = "";

        std::vector<uint8_t> bytes = encodeTextEmoteNotification(in);
        // 8 + 4 + 4 + 4 + 1(just the NUL) = 21.
        CHECK(bytes.size() == 8 + 4 + 4 + 4 + 1);

        ByteReader r(bytes.data(), bytes.size());
        TextEmoteNotification out = decodeTextEmoteNotification(r);
        CHECK(r.remaining() == 0);
        CHECK(out.senderGuid == 42);
        CHECK(out.textEmote == 34 && out.emoteNum == 5);
        CHECK(out.targetName.empty());
    }
}
