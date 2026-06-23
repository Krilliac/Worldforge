/*
 * WorldForge <-> mangos-zero bridge (server-side) -- SKETCH. See README.md.
 *
 * mangos headers referenced (current tree): World, Map, MapManager, ObjectMgr,
 * ObjectAccessor, Player, Creature, WorldPacket, MotionMaster. Adjust include
 * paths to your checkout. Lines that call mangos are marked // MANGOS.
 */
#include "WorldForgeBridge.h"

// --- mangos-zero includes (server tree) ---------------------------------
// #include "World/World.h"
// #include "Maps/Map.h"
// #include "Maps/MapManager.h"
// #include "Globals/ObjectAccessor.h"
// #include "Entities/Creature.h"
// #include "Entities/Player.h"
// #include "Server/WorldPacket.h"
// #include "MotionGenerators/MotionMaster.h"
// #include "Tools/Language.h"

#include "byte_reader.hpp"

WorldForgeBridge& WorldForgeBridge::instance() {
    static WorldForgeBridge s;
    return s;
}

// ----------------------------------------------------------------------------
// Networking (acceptor thread) -- replace the raw-socket stub with your ACE
// reactor / RASocket pattern; it must ONLY enqueue (no world-state access).
// ----------------------------------------------------------------------------
void WorldForgeBridge::Start(uint16_t /*port*/) {
    m_running = true;
    // m_acceptor = std::thread([this, port]{ ... accept(); read length-prefixed
    //   frames; QueueFrame(frame); ... }); // bind 127.0.0.1, auth >= SEC_ADMINISTRATOR
}

void WorldForgeBridge::Stop() {
    m_running = false;
    if (m_acceptor.joinable()) m_acceptor.join();
}

void WorldForgeBridge::QueueFrame(std::vector<uint8_t> frame) {
    std::lock_guard<std::mutex> g(m_lock);
    m_in.push_back(std::move(frame));
}

// ----------------------------------------------------------------------------
// Drain (world thread, inside World::Update next to ProcessCliCommands()).
// ----------------------------------------------------------------------------
void WorldForgeBridge::Process() {
    std::deque<std::vector<uint8_t>> batch;
    { std::lock_guard<std::mutex> g(m_lock); batch.swap(m_in); }

    for (auto& bytes : batch) {
        // Frames may arrive coalesced; decode them one after another.
        size_t off = 0;
        while (off + 6 <= bytes.size()) {
            std::vector<uint8_t> slice(bytes.begin() + off, bytes.end());
            wf::EditorFrame f; size_t used = 0;
            if (!wf::readFrame(slice, f, used)) break;   // short read -> wait for more
            Dispatch(f);
            off += used;
        }
    }
}

void WorldForgeBridge::Dispatch(const wf::EditorFrame& f) {
    using namespace wf;
    switch (f.opcode) {
        // ---- object edits ----
        case EDITOR_MOVE_OBJECT:    Handle(decodeMoveObject(f.payload));    break;
        case EDITOR_SPAWN_CREATURE: Handle(decodeSpawnCreature(f.payload)); break;
        case EDITOR_DESPAWN:        Handle(decodeDespawn(f.payload));       break;
        case EDITOR_SET_WAYPOINTS:  Handle(decodeSetWaypoints(f.payload));  break;
        case EDITOR_OVERRIDE_LIGHT: Handle(decodeOverrideLight(f.payload)); break;

        // ---- atmosphere / world FX: realise -> broadcast to scope ----
        case EDITOR_FX_WEATHER:    { auto o = decodeWeatherFx(f.payload);    Broadcast(realise(o), o.target); SendAck(o.opId, 0); break; }
        case EDITOR_FX_SOUND:      { auto o = decodeSoundFx(f.payload);      Broadcast(realise(o), o.target); SendAck(o.opId, 0); break; }
        case EDITOR_FX_CINEMATIC:  { auto o = decodeCinematicFx(f.payload);  Broadcast(realise(o), o.target); SendAck(o.opId, 0); break; }
        case EDITOR_FX_WORLDSTATE: { auto o = decodeWorldStateFx(f.payload); Broadcast(realise(o), o.target); SendAck(o.opId, 0); break; }
        case EDITOR_FX_SCREENMSG:  { auto o = decodeScreenMsgFx(f.payload);  Broadcast(realise(o), o.target); SendAck(o.opId, 0); break; }
        case EDITOR_FX_ZONEATTACK: { auto o = decodeZoneAttackFx(f.payload); wf::FxTarget all; all.scope = FxScope::Server; Broadcast(realise(o), all); SendAck(o.opId, 0); break; }
        case EDITOR_FX_TIMESPEED:  { auto o = decodeTimeSpeedFx(f.payload);  wf::FxTarget all; all.scope = FxScope::Server; Broadcast(realise(o), all); SendAck(o.opId, 0); break; }

        // ---- debug stream: editor -> server is normally the other way, but a
        //      relay could echo .debug vis captures down to the viewer here. ----
        default: break;
    }
}

// ----------------------------------------------------------------------------
// Object edits -- authoritative mangos mutations (visibility handled for us).
//
// THREADING: with MapUpdate.Threads > 0 a creature is owned by its map's worker.
// Resolve the Map and post these into that Map's update (Messager<Map> / a
// per-map queue) instead of mutating from the world thread. Shown inline for
// brevity.
// ----------------------------------------------------------------------------
void WorldForgeBridge::Handle(const wf::MoveObject& op) {
    // MANGOS:
    // ObjectGuid guid(op.guid);
    // if (Creature* c = sObjectAccessor.GetCreature(guid /*, anchor*/)) {
    //     c->GetMap()->CreatureRelocation(c, op.pos.x, op.pos.y, op.pos.z, op.orientation);
    //     SendAck(op.opId, 0); return;
    // }
    // if (Player* p = sObjectAccessor.FindPlayer(guid)) { p->TeleportTo(...); }
    SendAck(op.opId, 0);
}

void WorldForgeBridge::Handle(const wf::SpawnCreature& op) {
    // MANGOS:
    // Map* map = sMapMgr.FindMap(op.mapId);
    // if (map) { /* an anchor WorldObject in the map */->SummonCreature(
    //     op.entry, op.pos.x, op.pos.y, op.pos.z, op.orientation,
    //     TEMPSPAWN_MANUAL_DESPAWN, 0); }   // or persist via creature table (db_export)
    SendAck(op.opId, 0);
}

void WorldForgeBridge::Handle(const wf::Despawn& op) {
    // MANGOS:
    // if (Creature* c = sObjectAccessor.GetCreature(ObjectGuid(op.guid)))
    //     c->ForcedDespawn(0);             // -> SMSG_DESTROY_OBJECT to in-range
    SendAck(op.opId, 0);
}

void WorldForgeBridge::Handle(const wf::SetWaypoints& op) {
    // MANGOS:
    // if (Creature* c = sObjectAccessor.GetCreature(ObjectGuid(op.guid))) {
    //     // either an ad-hoc in-memory path:
    //     std::vector<G3D::Vector3> path;
    //     for (auto& p : op.path) path.emplace_back(p.x, p.y, p.z);
    //     c->GetMotionMaster()->MovePath(path, /*cyclic*/ true);
    //     // or persist nodes via sWaypointMgr.AddNode + MoveWaypoint, and
    //     //   db_export::waypointInserts() for durability.
    // }
    SendAck(op.opId, 0);
}

void WorldForgeBridge::Handle(const wf::OverrideLight& op) {
    // 0x411 has no vanilla client handler -- translate to a client-visible effect.
    // e.g. drive weather, swap the zone Light.dbc association, or set the editor's
    // own viewport light. Example: nudge weather for a mood shift.
    // wf::FxTarget t = { op.scope, op.targetGuid, op.zoneId };
    // Broadcast(wf::buildWeather(wf::WeatherType::Fine, 0.0f, 0, true), t);
    SendAck(op.opId, 0);
}

// ----------------------------------------------------------------------------
// Broadcast SMSG bytes to a scope.
// ----------------------------------------------------------------------------
WorldPacket WorldForgeBridge::ToWorldPacket(const std::vector<uint8_t>& framed) {
    wf::ServerHeader h = wf::readServerHeader(framed.data());
    // MANGOS: WorldPacket p(h.opcode, h.payloadLen);
    //         p.append(framed.data() + 4, h.payloadLen);
    //         return p;
    (void)h;
    return WorldPacket();   // placeholder
}

void WorldForgeBridge::Broadcast(const std::vector<uint8_t>& smsg, const wf::FxTarget& tgt) {
    // WorldPacket p = ToWorldPacket(smsg);
    switch (tgt.scope) {
        case wf::FxScope::Self:
            // MANGOS: editing GM's session -> session->SendPacket(&p);
            break;
        case wf::FxScope::Target:
            // MANGOS: if (Player* pl = sObjectAccessor.FindPlayer(ObjectGuid(tgt.guid)))
            //             pl->GetSession()->SendPacket(&p);
            break;
        case wf::FxScope::Zone:
            // MANGOS: iterate sessions; for each in-world Player with
            //   GetZoneId() == tgt.zoneId -> SendPacket(&p). (Or Map::MessageBroadcast.)
            break;
        case wf::FxScope::Server:
            // MANGOS: sWorld.SendGlobalMessage(&p);  // all connected sessions
            break;
    }
    (void)smsg;
}

void WorldForgeBridge::SendAck(uint32_t opId, uint8_t status) {
    std::vector<uint8_t> ack = wf::encode(wf::Ack{ opId, status });
    // write ack back over the editor socket (acceptor side owns the fd).
    (void)ack;
}

// Suppress -Wunused for the FX template until specialised per Fx type.
template <class Fx>
void WorldForgeBridge::HandleFx(const Fx&, const wf::FxTarget&) {}
