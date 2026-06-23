#include "test.hpp"
#include "editor_bridge.hpp"
#include "fxbridge.hpp"
#include "clientfx.hpp"
#include "db_export.hpp"
#include "worldproto.hpp"
#include "byte_reader.hpp"

#include <vector>

using namespace wf;

// Stand in for the server side: decode an editor frame and either record the
// authoritative object edit or produce the client-facing SMSG bytes an FX op
// broadcasts. Proves the whole module chain composes end to end.
namespace {

struct ServerSim {
    // object-edit results
    bool   spawned = false;   uint32_t spawnEntry = 0; Vec3 spawnPos;
    bool   moved   = false;   uint64_t movedGuid = 0;  Vec3 movePos;
    int    waypointCount = 0;
    bool   despawned = false;
    // last realised SMSG (opcode + body)
    uint16_t lastSmsgOpcode = 0;
    std::vector<uint8_t> lastSmsgBody;
    int    smsgCount = 0;

    void recordSmsg(const std::vector<uint8_t>& pkt) {
        ServerHeader h = readServerHeader(pkt.data());
        lastSmsgOpcode = h.opcode;
        lastSmsgBody.assign(pkt.begin() + 4, pkt.begin() + 4 + h.payloadLen);
        ++smsgCount;
    }

    void dispatch(const EditorFrame& f) {
        switch (f.opcode) {
            case EDITOR_SPAWN_CREATURE: {
                auto o = decodeSpawnCreature(f.payload);
                spawned = true; spawnEntry = o.entry; spawnPos = o.pos; break;
            }
            case EDITOR_MOVE_OBJECT: {
                auto o = decodeMoveObject(f.payload);
                moved = true; movedGuid = o.guid; movePos = o.pos; break;
            }
            case EDITOR_SET_WAYPOINTS: {
                auto o = decodeSetWaypoints(f.payload);
                waypointCount = static_cast<int>(o.path.size()); break;
            }
            case EDITOR_DESPAWN: despawned = true; break;
            case EDITOR_FX_WEATHER:   recordSmsg(realise(decodeWeatherFx(f.payload)));   break;
            case EDITOR_FX_SOUND:     recordSmsg(realise(decodeSoundFx(f.payload)));     break;
            case EDITOR_FX_SCREENMSG: recordSmsg(realise(decodeScreenMsgFx(f.payload))); break;
            default: break;
        }
    }

    // Drain a byte stream of one or more coalesced frames (the real socket case).
    void drain(const std::vector<uint8_t>& stream) {
        size_t off = 0;
        while (off + 6 <= stream.size()) {
            std::vector<uint8_t> slice(stream.begin() + off, stream.end());
            EditorFrame f; size_t used = 0;
            if (!readFrame(slice, f, used)) break;
            dispatch(f);
            off += used;
        }
    }
};

} // namespace

void test_integration() {
    std::printf("[integration]\n");

    ServerSim srv;

    // The editor records a session: spawn an NPC, move it, give it a patrol,
    // then set the mood -- weather + a sound + an area-trigger banner. All ops
    // are concatenated into one byte stream, exactly as a socket would deliver.
    std::vector<uint8_t> stream;
    auto append = [&](const std::vector<uint8_t>& pkt) {
        stream.insert(stream.end(), pkt.begin(), pkt.end());
    };

    SpawnCreature sp; sp.entry = 299; sp.mapId = 0; sp.pos = {-9449.0f, 64.0f, 56.0f};
    sp.orientation = 0.0f; sp.opId = 1;
    append(encode(sp));

    MoveObject mv; mv.guid = 0xF130000000000001ull; mv.pos = {-9440.0f, 70.0f, 56.5f};
    mv.opId = 2;
    append(encode(mv));

    SetWaypoints wp; wp.guid = mv.guid; wp.opId = 3;
    wp.path = { {-9440,70,56}, {-9430,75,56}, {-9420,70,56} };
    append(encode(wp));

    WeatherFx wx; wx.type = WeatherType::Rain; wx.grade = 0.8f;
    wx.target.scope = FxScope::Zone; wx.target.zoneId = 12; wx.opId = 4;
    append(encode(wx));

    SoundFx snd; snd.soundId = 8585; snd.music = true; snd.target.scope = FxScope::Zone;
    snd.target.zoneId = 12; snd.opId = 5;
    append(encode(snd));

    ScreenMsgFx msg; msg.kind = 0; msg.text = "The storm rolls in."; msg.opId = 6;
    msg.target.scope = FxScope::Zone; msg.target.zoneId = 12;
    append(encode(msg));

    // Server drains the whole stream in one go.
    srv.drain(stream);

    // ---- object edits landed with correct fields ----
    CHECK(srv.spawned && srv.spawnEntry == 299);
    CHECK_APPROX(srv.spawnPos.x, -9449.0f);
    CHECK(srv.moved && srv.movedGuid == 0xF130000000000001ull);
    CHECK_APPROX(srv.movePos.y, 70.0f);
    CHECK(srv.waypointCount == 3);

    // ---- three FX ops realised to three client-facing SMSGs ----
    CHECK(srv.smsgCount == 3);
    // The last FX in the stream was the area-trigger banner.
    CHECK(srv.lastSmsgOpcode == SMSG_AREA_TRIGGER_MESSAGE);
    {
        ByteReader r(srv.lastSmsgBody);
        CHECK(r.u32() == 20);                 // strlen("The storm rolls in.")+1
    }

    // ---- the spawn also persists to SQL for durability (db_export) ----
    CreatureSpawn cs;
    cs.guid = 50001; cs.entry = sp.entry; cs.mapId = sp.mapId;
    cs.pos = sp.pos; cs.orientation = sp.orientation;
    std::string sql = creatureInsert(cs);
    CHECK(sql.find("INSERT INTO creature") != std::string::npos);
    CHECK(sql.find("299") != std::string::npos);
    std::string wpsql = waypointInserts(cs.guid, wp.path, 0);
    CHECK(wpsql.find("MovementType = 2") != std::string::npos);

    // ---- a truncated tail in the stream is left for the next read ----
    std::vector<uint8_t> partial = encode(ZoneAttackFx{1519, 9});
    partial.resize(partial.size() - 2);       // chop the last 2 bytes
    ServerSim srv2;
    srv2.drain(partial);
    CHECK(srv2.smsgCount == 0);                // nothing dispatched from a short frame
}
