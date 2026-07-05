#pragma once
// ---------------------------------------------------------------------------
// Text-emote codec for vanilla 1.12.1 (build 5875). A "text emote" is the
// /dance, /wave, /flirt family: the client sends an emote id (+ a random
// variation number and an optional target guid), and the server broadcasts a
// rendered notification to everyone in listen range so their clients play the
// animation and print the "Thrall waves at Grommash." line.
//
//   client -> CMSG_TEXT_EMOTE { u32 textEmote, u32 emoteNum, u64 targetGuid }
//   server -> SMSG_TEXT_EMOTE { u64 senderGuid, u32 textEmote, u32 emoteNum,
//                               u32 nameLen, cstring targetName }
//
// Vanilla-specific details (verified vs mangos-zero ChatHandler
// HandleTextEmoteOpcode + EmoteChatBuilder):
//   * On the CMSG side textEmote and emoteNum are both u32, and the target
//     guid is streamed LAST (after the two ids) as a raw u64 (not packed).
//   * On the SMSG side the target name is length-prefixed: a u32 nameLen that
//     counts the terminator (strlen + 1), immediately followed by that many
//     bytes -- the NUL-terminated name. When there is no named target the
//     server still writes nameLen == 1 and a single 0 byte (an empty cstring),
//     so a name field is ALWAYS present on the wire.
//
// Pure over byte_reader/byte_writer -- no sockets, no opcode enums -- so the
// exchange round-trips deterministically (tests/test_text_emote.cpp). Cross-
// checked vs the GPL mangos-zero vanilla source used as a fact reference; our
// own clean-room code, nothing copied.
//
// Opcode values (vanilla 1.12.1): CMSG_TEXT_EMOTE = 0x104,
//                                 SMSG_TEXT_EMOTE = 0x105.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "byte_reader.hpp"
#include "byte_writer.hpp"
#include "net/query.hpp"    // writeCString / readCString

namespace wf {

// ---- CMSG_TEXT_EMOTE (client -> server) -------------------------------------
// What the client emits when the player types /dance, /wave, etc. The target
// guid is 0 when the emote is untargeted (e.g. a plain /dance).
struct TextEmoteRequest {
    uint32_t textEmote  = 0;   // EmotesText.dbc id (the /command chosen)
    uint32_t emoteNum   = 0;   // random variation index the client rolled
    uint64_t targetGuid = 0;   // raw u64; 0 when untargeted
};

inline std::vector<uint8_t> encodeTextEmoteRequest(const TextEmoteRequest& e) {
    ByteWriter w;
    w.u32(e.textEmote);
    w.u32(e.emoteNum);
    w.u64(e.targetGuid);   // guid streamed LAST in vanilla
    return w.data();
}

inline TextEmoteRequest decodeTextEmoteRequest(ByteReader& r) {
    TextEmoteRequest e;
    e.textEmote  = r.u32();
    e.emoteNum   = r.u32();
    e.targetGuid = r.u64();
    return e;
}

// ---- SMSG_TEXT_EMOTE (server -> client) -------------------------------------
// The broadcast the server sends to nearby players. senderGuid is the emoting
// player; targetName is the localized name of the target (empty when there was
// no target). nameLen on the wire is strlen(targetName)+1 (includes the NUL).
struct TextEmoteNotification {
    uint64_t    senderGuid = 0;
    uint32_t    textEmote  = 0;
    uint32_t    emoteNum   = 0;
    std::string targetName;    // empty => nameLen 1, single NUL on the wire
};

inline std::vector<uint8_t> encodeTextEmoteNotification(const TextEmoteNotification& e) {
    ByteWriter w;
    w.u64(e.senderGuid);
    w.u32(e.textEmote);
    w.u32(e.emoteNum);
    w.u32(static_cast<uint32_t>(e.targetName.size() + 1));  // nameLen incl. NUL
    writeCString(w, e.targetName);                          // name + terminator
    return w.data();
}

inline TextEmoteNotification decodeTextEmoteNotification(ByteReader& r) {
    TextEmoteNotification e;
    e.senderGuid = r.u64();
    e.textEmote  = r.u32();
    e.emoteNum   = r.u32();
    (void)r.u32();                    // nameLen: the cstring below self-delimits
    e.targetName = readCString(r);
    return e;
}

}  // namespace wf
