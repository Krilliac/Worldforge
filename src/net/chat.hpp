#pragma once
// ---------------------------------------------------------------------------
// Chat message codec for vanilla 1.12.1 (build 5875). SMSG_MESSAGECHAT is a
// type-switched packet: a common header (chat type + language), then a
// per-type sender block, then a length-prefixed message and a tag byte. This
// reconstructs the server->client form (what the client renders in its chat
// frame); the client->server CMSG form is a follow-up.
//
//   u8  type            (ChatMsg: SAY/YELL/PARTY/GUILD/WHISPER/CHANNEL/...)
//   u32 language        (ChatLanguage: UNIVERSAL/ORCISH/COMMON/...)
//   -- sender block, by type --
//     SAY/PARTY/YELL          : u64 senderGuid, u64 senderGuid   (guid twice)
//     CHANNEL                 : cstring channel, u32 rank, u64 senderGuid
//     MONSTER_SAY/MONSTER_YELL: u64 senderGuid, lpstr name, u64 targetGuid
//     MONSTER_WHISPER/EMOTE   : lpstr name, u64 targetGuid
//     default (GUILD/WHISPER/EMOTE/SYSTEM/...) : u64 senderGuid
//   lpstr message       (u32 len = strlen+1, then the NUL-terminated text)
//   u8  tag             (ChatTag: gm / afk / dnd)
//
// "lpstr" = a u32 length (including the terminator) followed by the string and
// its NUL -- redundant, but that's the wire format. Verified vs the mangos-zero
// ChatHandler::BuildChatPacket (GPL fact reference); our own code, no copy.
// Pure over byte_reader/byte_writer -> round-trips (tests/test_chat.cpp).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"    // writeCString / readCString
#include "worldproto.hpp"   // CMSG/SMSG_MESSAGECHAT

namespace wf {

// ChatMsg values (vanilla 1.12.1). Only the ones the codec branches on are named.
enum ChatMsg : uint8_t {
    CHAT_MSG_SAY             = 0x00,
    CHAT_MSG_PARTY           = 0x01,
    CHAT_MSG_GUILD           = 0x03,
    CHAT_MSG_YELL            = 0x05,
    CHAT_MSG_WHISPER         = 0x06,
    CHAT_MSG_EMOTE           = 0x08,
    CHAT_MSG_SYSTEM          = 0x0A,
    CHAT_MSG_MONSTER_SAY     = 0x0B,
    CHAT_MSG_MONSTER_YELL    = 0x0C,
    CHAT_MSG_MONSTER_EMOTE   = 0x0D,
    CHAT_MSG_CHANNEL         = 0x0E,
    CHAT_MSG_MONSTER_WHISPER = 0x1A,
};

enum ChatLanguage : uint32_t {
    LANG_UNIVERSAL = 0,
    LANG_ORCISH    = 1,
    LANG_COMMON    = 7,
};

// A decoded chat message. Only the fields relevant to `type` are meaningful.
struct ChatMessage {
    uint8_t     type       = CHAT_MSG_SAY;
    uint32_t    language   = LANG_UNIVERSAL;
    uint64_t    senderGuid = 0;
    uint64_t    targetGuid = 0;      // monster-* forms
    std::string senderName;          // monster-* forms (inline)
    std::string channelName;         // CHANNEL only
    uint32_t    channelRank = 0;     // CHANNEL only
    std::string text;
    uint8_t     tag        = 0;      // ChatTag (gm/afk/dnd)
};

// A length-prefixed WoW string: u32 (len incl. terminator) + cstring.
inline void writeLpStr(ByteWriter& w, const std::string& s) {
    w.u32(static_cast<uint32_t>(s.size() + 1));
    writeCString(w, s);
}
inline std::string readLpStr(ByteReader& r) {
    (void)r.u32();               // length (incl. NUL); the cstring self-delimits
    return readCString(r);
}

inline std::vector<uint8_t> encodeChatMessage(const ChatMessage& m) {
    ByteWriter w;
    w.u8(m.type);
    w.u32(m.language);
    switch (m.type) {
        case CHAT_MSG_SAY:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_YELL:
            w.u64(m.senderGuid);
            w.u64(m.senderGuid);          // guid streamed twice for these
            break;
        case CHAT_MSG_CHANNEL:
            writeCString(w, m.channelName);   // bare cstring (no length prefix)
            w.u32(m.channelRank);
            w.u64(m.senderGuid);
            break;
        case CHAT_MSG_MONSTER_SAY:
        case CHAT_MSG_MONSTER_YELL:
            w.u64(m.senderGuid);
            writeLpStr(w, m.senderName);
            w.u64(m.targetGuid);
            break;
        case CHAT_MSG_MONSTER_WHISPER:
        case CHAT_MSG_MONSTER_EMOTE:
            writeLpStr(w, m.senderName);
            w.u64(m.targetGuid);
            break;
        default:                          // GUILD/WHISPER/EMOTE/SYSTEM/...
            w.u64(m.senderGuid);
            break;
    }
    writeLpStr(w, m.text);
    w.u8(m.tag);
    return w.data();
}

inline ChatMessage decodeChatMessage(ByteReader& r) {
    ChatMessage m;
    m.type     = r.u8();
    m.language = r.u32();
    switch (m.type) {
        case CHAT_MSG_SAY:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_YELL:
            m.senderGuid = r.u64();
            (void)r.u64();                // duplicate guid
            break;
        case CHAT_MSG_CHANNEL:
            m.channelName = readCString(r);
            m.channelRank = r.u32();
            m.senderGuid  = r.u64();
            break;
        case CHAT_MSG_MONSTER_SAY:
        case CHAT_MSG_MONSTER_YELL:
            m.senderGuid = r.u64();
            m.senderName = readLpStr(r);
            m.targetGuid = r.u64();
            break;
        case CHAT_MSG_MONSTER_WHISPER:
        case CHAT_MSG_MONSTER_EMOTE:
            m.senderName = readLpStr(r);
            m.targetGuid = r.u64();
            break;
        default:
            m.senderGuid = r.u64();
            break;
    }
    m.text = readLpStr(r);
    m.tag  = r.u8();
    return m;
}

}  // namespace wf
