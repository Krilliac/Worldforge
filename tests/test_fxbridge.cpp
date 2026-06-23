#include "test.hpp"
#include "fxbridge.hpp"
#include "editor_bridge.hpp"
#include "clientfx.hpp"
#include "worldproto.hpp"
#include "byte_reader.hpp"

#include <string>
#include <vector>

using namespace wf;

namespace {
uint16_t smsgOpcode(const std::vector<uint8_t>& pkt) { return readServerHeader(pkt.data()).opcode; }
std::vector<uint8_t> smsgBody(const std::vector<uint8_t>& pkt) {
    ServerHeader h = readServerHeader(pkt.data());
    return std::vector<uint8_t>(pkt.begin() + 4, pkt.begin() + 4 + h.payloadLen);
}
} // namespace

void test_fxbridge() {
    std::printf("[fxbridge]\n");

    EditorFrame fr; size_t used = 0;

    // --- weather: bridge round-trip, then realise -> SMSG_WEATHER -----------
    {
        WeatherFx w; w.type = WeatherType::Snow; w.grade = 0.5f; w.soundId = 0;
        w.instant = true; w.target.scope = FxScope::Zone; w.target.zoneId = 12; w.opId = 3;
        CHECK(readFrame(encode(w), fr, used) && fr.opcode == EDITOR_FX_WEATHER);
        WeatherFx d = decodeWeatherFx(fr.payload);
        CHECK(d.type == WeatherType::Snow && d.instant);
        CHECK(d.target.scope == FxScope::Zone && d.target.zoneId == 12 && d.opId == 3);
        CHECK_APPROX(d.grade, 0.5f);

        std::vector<uint8_t> smsg = realise(d);
        CHECK(smsgOpcode(smsg) == SMSG_WEATHER);
        std::vector<uint8_t> body = smsgBody(smsg);
        ByteReader r(body);
        CHECK(r.u32() == 2);                       // Snow
        CHECK_APPROX(r.f32(), 0.5f);
        CHECK(r.u32() == 0);                       // soundId
        CHECK(r.u8() == 1);                        // instant
    }

    // --- sound vs music selection -------------------------------------------
    {
        SoundFx s; s.soundId = 8585; s.music = false; s.target.scope = FxScope::Self;
        CHECK(readFrame(encode(s), fr, used) && fr.opcode == EDITOR_FX_SOUND);
        SoundFx d = decodeSoundFx(fr.payload);
        CHECK(d.soundId == 8585 && !d.music);
        CHECK(smsgOpcode(realise(d)) == SMSG_PLAY_SOUND);
        d.music = true;
        CHECK(smsgOpcode(realise(d)) == SMSG_PLAY_MUSIC);
    }

    // --- cinematic ----------------------------------------------------------
    {
        CinematicFx c; c.cinematicId = 81; c.target.scope = FxScope::Self; c.opId = 7;
        CHECK(readFrame(encode(c), fr, used) && fr.opcode == EDITOR_FX_CINEMATIC);
        CinematicFx d = decodeCinematicFx(fr.payload);
        CHECK(d.cinematicId == 81 && d.opId == 7);
        std::vector<uint8_t> smsg = realise(d);
        CHECK(smsgOpcode(smsg) == SMSG_TRIGGER_CINEMATIC);
        CHECK(ByteReader(smsgBody(smsg)).u32() == 81);
    }

    // --- world state --------------------------------------------------------
    {
        WorldStateFx ws; ws.field = 0x8D8; ws.value = 5; ws.target.scope = FxScope::Zone; ws.target.zoneId = 1519;
        CHECK(readFrame(encode(ws), fr, used) && fr.opcode == EDITOR_FX_WORLDSTATE);
        WorldStateFx d = decodeWorldStateFx(fr.payload);
        CHECK(d.field == 0x8D8 && d.value == 5);
        std::vector<uint8_t> smsg = realise(d);
        CHECK(smsgOpcode(smsg) == SMSG_UPDATE_WORLD_STATE);
        std::vector<uint8_t> body = smsgBody(smsg);
        ByteReader r(body);
        CHECK(r.u32() == 0x8D8 && r.u32() == 5);
    }

    // --- screen message: three kinds map to three opcodes -------------------
    {
        ScreenMsgFx m; m.kind = 0; m.text = "Pull!"; m.target.scope = FxScope::Zone;
        CHECK(readFrame(encode(m), fr, used) && fr.opcode == EDITOR_FX_SCREENMSG);
        ScreenMsgFx d = decodeScreenMsgFx(fr.payload);
        CHECK(d.text == "Pull!");
        CHECK(smsgOpcode(realise(d)) == SMSG_AREA_TRIGGER_MESSAGE);
        d.kind = 1; CHECK(smsgOpcode(realise(d)) == SMSG_NOTIFICATION);
        d.kind = 2; d.type = 1; CHECK(smsgOpcode(realise(d)) == SMSG_SERVER_MESSAGE);
    }

    // --- time speed (server-wide) -------------------------------------------
    {
        TimeSpeedFx t; t.packedDate = 0x12345678; t.speed = 0.05f; t.opId = 1;
        CHECK(readFrame(encode(t), fr, used) && fr.opcode == EDITOR_FX_TIMESPEED);
        TimeSpeedFx d = decodeTimeSpeedFx(fr.payload);
        CHECK(d.packedDate == 0x12345678u);
        CHECK_APPROX(d.speed, 0.05f);
        std::vector<uint8_t> smsg = realise(d);
        CHECK(smsgOpcode(smsg) == SMSG_LOGIN_SETTIMESPEED);
        std::vector<uint8_t> body = smsgBody(smsg);
        ByteReader r(body);
        CHECK(r.u32() == 0x12345678u);
        CHECK_APPROX(r.f32(), 0.05f);
    }

    // --- zone under attack --------------------------------------------------
    {
        ZoneAttackFx z; z.zoneId = 1519; z.opId = 2;
        CHECK(readFrame(encode(z), fr, used) && fr.opcode == EDITOR_FX_ZONEATTACK);
        ZoneAttackFx d = decodeZoneAttackFx(fr.payload);
        CHECK(d.zoneId == 1519);
        std::vector<uint8_t> smsg = realise(d);
        CHECK(smsgOpcode(smsg) == SMSG_ZONE_UNDER_ATTACK);
        CHECK(ByteReader(smsgBody(smsg)).u32() == 1519);
    }
}
