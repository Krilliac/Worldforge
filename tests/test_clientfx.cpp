#include "test.hpp"
#include "clientfx.hpp"
#include "worldproto.hpp"
#include "byte_reader.hpp"

#include <string>
#include <vector>

using namespace wf;

namespace {
// Split a framed server packet into (opcode, body reader).
struct Parsed { uint16_t opcode; std::vector<uint8_t> body; };
Parsed parse(const std::vector<uint8_t>& pkt) {
    ServerHeader h = readServerHeader(pkt.data());
    return { h.opcode, std::vector<uint8_t>(pkt.begin() + 4, pkt.begin() + 4 + h.payloadLen) };
}
} // namespace

void test_clientfx() {
    std::printf("[clientfx]\n");

    // --- audio: id-only and id-then-guid ordering ---------------------------
    {
        Parsed p = parse(buildPlaySound(8585));
        CHECK(p.opcode == SMSG_PLAY_SOUND);
        ByteReader r(p.body);
        CHECK(r.u32() == 8585 && r.remaining() == 0);
    }
    {
        Parsed p = parse(buildPlayObjectSound(1234, 0xF130000000000ABCull));
        CHECK(p.opcode == SMSG_PLAY_OBJECT_SOUND);
        ByteReader r(p.body);
        CHECK(r.u32() == 1234);                       // sound id FIRST
        CHECK(r.u64() == 0xF130000000000ABCull);      // then the source guid
    }
    {
        Parsed p = parse(buildPlayMusic(53));
        CHECK(p.opcode == SMSG_PLAY_MUSIC);
        CHECK(ByteReader(p.body).u32() == 53);
    }

    // --- cinematic / spell visuals / GO anim --------------------------------
    {
        Parsed p = parse(buildTriggerCinematic(81));
        CHECK(p.opcode == SMSG_TRIGGER_CINEMATIC);
        CHECK(ByteReader(p.body).u32() == 81);
    }
    {
        Parsed p = parse(buildPlaySpellVisual(0x42, 362));
        CHECK(p.opcode == SMSG_PLAY_SPELL_VISUAL);
        ByteReader r(p.body);
        CHECK(r.u64() == 0x42 && r.u32() == 362);     // guid THEN kit id
    }
    {
        Parsed p = parse(buildGameObjectCustomAnim(0x99, 0));
        CHECK(p.opcode == SMSG_GAMEOBJECT_CUSTOM_ANIM);
        ByteReader r(p.body);
        CHECK(r.u64() == 0x99 && r.u32() == 0);
    }

    // --- text: area-trigger has a length+NUL, notification is bare cstr ------
    {
        Parsed p = parse(buildAreaTriggerMessage("Hello"));
        CHECK(p.opcode == SMSG_AREA_TRIGGER_MESSAGE);
        ByteReader r(p.body);
        CHECK(r.u32() == 6);                           // strlen("Hello")+1
        CHECK(p.body.size() == 4 + 6);                 // len field + "Hello\0"
        CHECK(p.body.back() == 0);                     // NUL-terminated
    }
    {
        Parsed p = parse(buildNotification("Saved."));
        CHECK(p.opcode == SMSG_NOTIFICATION);
        CHECK(p.body.size() == 7);                     // "Saved." + NUL, no length
        CHECK(p.body.back() == 0);
        CHECK(p.body[0] == 'S');
    }
    {
        Parsed p = parse(buildServerMessage(1, "shutdown"));
        CHECK(p.opcode == SMSG_SERVER_MESSAGE);
        ByteReader r(p.body);
        CHECK(r.u32() == 1);                           // type then cstr
        CHECK(p.body.back() == 0);
    }

    // --- minimap ping: guid + x + y -----------------------------------------
    {
        Parsed p = parse(buildMinimapPing(0x7, -123.5f, 456.25f));
        CHECK(p.opcode == MSG_MINIMAP_PING);
        ByteReader r(p.body);
        CHECK(r.u64() == 0x7);
        CHECK_APPROX(r.f32(), -123.5f);
        CHECK_APPROX(r.f32(), 456.25f);
    }

    // --- world state: single + init (count, no areaId) ----------------------
    {
        Parsed p = parse(buildUpdateWorldState(0x8D8, 3));
        CHECK(p.opcode == SMSG_UPDATE_WORLD_STATE);
        ByteReader r(p.body);
        CHECK(r.u32() == 0x8D8 && r.u32() == 3);
    }
    {
        std::vector<std::pair<uint32_t,uint32_t>> states = { {1,10}, {2,20}, {3,30} };
        Parsed p = parse(buildInitWorldStates(0, 1519, states));
        CHECK(p.opcode == SMSG_INIT_WORLD_STATES);
        ByteReader r(p.body);
        CHECK(r.u32() == 0);                           // mapId
        CHECK(r.u32() == 1519);                        // zoneId (no areaId after)
        CHECK(r.u16() == 3);                           // count
        CHECK(r.u32() == 1 && r.u32() == 10);
        CHECK(r.u32() == 2 && r.u32() == 20);
        CHECK(r.u32() == 3 && r.u32() == 30);
        CHECK(r.remaining() == 0);
    }

    // --- weather: type, grade, soundId (vanilla), transition ----------------
    {
        Parsed p = parse(buildWeather(WeatherType::Rain, 0.75f, 8557, /*instant*/false));
        CHECK(p.opcode == SMSG_WEATHER);
        ByteReader r(p.body);
        CHECK(r.u32() == 1);                            // Rain
        CHECK_APPROX(r.f32(), 0.75f);
        CHECK(r.u32() == 8557);                         // vanilla-only soundId
        CHECK(r.u8() == 0);                             // smooth transition
        CHECK(r.remaining() == 0);
    }

    // --- zone under attack + login time speed -------------------------------
    {
        Parsed p = parse(buildZoneUnderAttack(1519));
        CHECK(p.opcode == SMSG_ZONE_UNDER_ATTACK);
        CHECK(ByteReader(p.body).u32() == 1519);
    }
    {
        // 2026-06-23 (Tue=2), 14:30. Verify the packing round-trips field by field.
        uint32_t packed = packTimeBitFields(2026, 5, 23, 2, 14, 30);
        CHECK(((packed >> 24) & 0xFF) == (2026 - 2000));   // field is year-2000
        CHECK(((packed >> 20) & 0x0F) == 5);
        CHECK(((packed >> 14) & 0x3F) == 22);          // mday-1
        CHECK(((packed >> 11) & 0x07) == 2);
        CHECK(((packed >> 6)  & 0x1F) == 14);
        CHECK((packed & 0x3F) == 30);

        Parsed p = parse(buildLoginSetTimeSpeed(packed));
        CHECK(p.opcode == SMSG_LOGIN_SETTIMESPEED);
        ByteReader r(p.body);
        CHECK(r.u32() == packed);
        CHECK_APPROX(r.f32(), 0.01666667f);
    }
}
