// ---------------------------------------------------------------------------
// SMSG_MESSAGECHAT codec round-trip tests (src/net/chat.hpp). The type-switched
// sender block + length-prefixed message + tag survive encode/decode for each
// chat form (say/yell, channel, monster-say, and the default single-guid form).
// ---------------------------------------------------------------------------
#include "test.hpp"

#include "net/chat.hpp"
#include "byte_reader.hpp"

using namespace wf;

static ChatMessage roundTrip(const ChatMessage& in) {
    std::vector<uint8_t> b = encodeChatMessage(in);
    ByteReader r(b.data(), b.size());
    ChatMessage out = decodeChatMessage(r);
    CHECK(r.remaining() == 0);       // consumed exactly what was written
    return out;
}

void test_chat() {
    std::printf("[net.chat]\n");

    // SAY: the guid is streamed twice; both language and text survive.
    {
        ChatMessage in;
        in.type = CHAT_MSG_SAY; in.language = LANG_COMMON;
        in.senderGuid = 0x00F1000000001234ull; in.text = "hello azeroth"; in.tag = 0;
        ChatMessage out = roundTrip(in);
        CHECK(out.type == CHAT_MSG_SAY && out.language == LANG_COMMON);
        CHECK(out.senderGuid == in.senderGuid && out.text == "hello azeroth");
        // 1(type)+4(lang)+16(guid x2)+4(len)+14("...\0")+1(tag) = 40
        CHECK(encodeChatMessage(in).size() == 1 + 4 + 16 + 4 + 14 + 1);
    }

    // CHANNEL: bare channel cstring + rank + one guid.
    {
        ChatMessage in;
        in.type = CHAT_MSG_CHANNEL; in.language = LANG_UNIVERSAL;
        in.channelName = "General"; in.channelRank = 2;
        in.senderGuid = 42; in.text = "lfg"; in.tag = 0;
        ChatMessage out = roundTrip(in);
        CHECK(out.type == CHAT_MSG_CHANNEL && out.channelName == "General");
        CHECK(out.channelRank == 2 && out.senderGuid == 42 && out.text == "lfg");
    }

    // MONSTER_SAY: inline length-prefixed sender name + a target guid.
    {
        ChatMessage in;
        in.type = CHAT_MSG_MONSTER_SAY; in.language = LANG_UNIVERSAL;
        in.senderGuid = 0xF13000000000ABCDull; in.senderName = "Hogger";
        in.targetGuid = 7; in.text = "Rrraaugh!"; in.tag = 0;
        ChatMessage out = roundTrip(in);
        CHECK(out.senderName == "Hogger" && out.senderGuid == in.senderGuid);
        CHECK(out.targetGuid == 7 && out.text == "Rrraaugh!");
    }

    // GUILD (default single-guid form) + an AFK tag round-trips.
    {
        ChatMessage in;
        in.type = CHAT_MSG_GUILD; in.language = LANG_ORCISH;
        in.senderGuid = 0x1122334455667788ull; in.text = "raid at 8"; in.tag = 1;
        ChatMessage out = roundTrip(in);
        CHECK(out.type == CHAT_MSG_GUILD && out.senderGuid == in.senderGuid);
        CHECK(out.language == LANG_ORCISH && out.tag == 1 && out.text == "raid at 8");
    }

    // Empty text still frames (len = 1 -> just the NUL).
    {
        ChatMessage in; in.type = CHAT_MSG_SYSTEM; in.text = "";
        ChatMessage out = roundTrip(in);
        CHECK(out.text.empty() && out.type == CHAT_MSG_SYSTEM);
    }

    // Opcode values match the vanilla table.
    CHECK(CMSG_MESSAGECHAT == 0x095);
    CHECK(SMSG_MESSAGECHAT == 0x096);
}
