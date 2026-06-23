#pragma once
// ---------------------------------------------------------------------------
// StubBridgeServer: a standalone, compilable server that speaks the WorldForge
// editor protocol -- the runnable analog of integration/mangos-zero's
// WorldForgeBridge, but backed by WorldSim instead of mangos (no mangos headers
// needed). It accepts editor connections, decodes EDITOR_* ops, applies them to
// the world, sends an Ack per op, and streams debug back (a marker per spawn, a
// nav path per SET_WAYPOINTS). This lets the FULL editor<->server round-trip run
// and be unit-tested here, and gives a target to point the real editor at.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "server/world_sim.hpp"

namespace wf {

class StubBridgeServer {
public:
    StubBridgeServer() = default;
    ~StubBridgeServer();
    StubBridgeServer(const StubBridgeServer&)            = delete;
    StubBridgeServer& operator=(const StubBridgeServer&) = delete;

    // Bind 127.0.0.1:port (port 0 -> ephemeral) and start accepting. Returns
    // false on bind/listen failure.
    bool start(uint16_t port);
    void stop();
    uint16_t port() const { return port_; }       // the bound port (after start)
    bool running() const { return running_.load(); }

    // Thread-safe snapshots of the simulated world (the client threads mutate it
    // under a lock).
    size_t aliveCount();
    bool   getObject(uint64_t guid, SimObject& out);
    std::vector<uint64_t> guids();
    FxLog  fxSnapshot();

private:
    void acceptLoop();
    void clientLoop(int fd);

    int                 listenFd_ = -1;
    uint16_t            port_ = 0;
    std::thread         acceptThread_;
    std::atomic<bool>   running_{false};

    WorldSim            world_;
    std::mutex          worldMtx_;
};

} // namespace wf
