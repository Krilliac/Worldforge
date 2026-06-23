/*
 * WorldForge <-> mangos-zero bridge (server-side).
 *
 * SKETCH: compiles inside a mangos-zero tree (needs its headers), not in the
 * WorldForge build. The WorldForge wire layer (wf::) is byte-exact + unit-tested;
 * the mangos calls use real current-tree API names. See README.md.
 */
#ifndef WORLDFORGE_BRIDGE_H
#define WORLDFORGE_BRIDGE_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

// WorldForge wire headers (vendored / on the include path).
#include "editor_bridge.hpp"
#include "fxbridge.hpp"
#include "clientfx.hpp"
#include "worldproto.hpp"

class WorldPacket;   // mangos
class Player;        // mangos

/// Receives WorldForge editor RPCs on a private TCP port and applies them to the
/// live world. Singleton; Start() once, Process() every world tick, Stop() on
/// shutdown.
class WorldForgeBridge
{
public:
    static WorldForgeBridge& instance();

    void Start(uint16_t port);     ///< spawn the acceptor thread
    void Stop();

    /// Called from the acceptor/IO thread for each complete frame received.
    /// Thread-safe (locks); only enqueues.
    void QueueFrame(std::vector<uint8_t> frame);

    /// Drain + apply queued frames. Call on the world thread inside World::Update,
    /// next to ProcessCliCommands().
    void Process();

private:
    WorldForgeBridge() = default;

    void Dispatch(const wf::EditorFrame& f);

    // Object edits (authoritative mangos mutations).
    void Handle(const wf::MoveObject& op);
    void Handle(const wf::SpawnCreature& op);
    void Handle(const wf::Despawn& op);
    void Handle(const wf::SetWaypoints& op);

    // Atmosphere / world FX: realise -> SMSG -> broadcast to scope.
    template <class Fx> void HandleFx(const Fx& op, const wf::FxTarget& tgt);
    void Handle(const wf::OverrideLight& op);

    // Send an ack frame back to the editor (op result).
    void SendAck(uint32_t opId, uint8_t status);

    // Broadcast verified SMSG bytes (from wf::realise/build*) to a scope.
    void Broadcast(const std::vector<uint8_t>& smsg, const wf::FxTarget& tgt);
    static WorldPacket ToWorldPacket(const std::vector<uint8_t>& framed);

    std::mutex                       m_lock;
    std::deque<std::vector<uint8_t>> m_in;
    std::thread                      m_acceptor;
    std::atomic<bool>                m_running{false};
    int                              m_listenFd{-1};
};

#endif // WORLDFORGE_BRIDGE_H
