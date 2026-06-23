#pragma once
// ---------------------------------------------------------------------------
// BridgeClient: the editor-side TCP client that ships framed EDITOR_* packets
// (the panels' emitted ops) to a running mangos-zero's WorldForge bridge and
// receives frames back (acks, the debug stream). A background thread reads bytes
// and splits them into complete EditorFrames; poll() drains the decoded frames
// on the UI thread. This closes the "author the live world" loop -- the panel
// produces an op, send() puts it on the wire, the server (integration/mangos-
// zero) applies it. Mirrors Spark's LiveEditBridge.
//
// Cross-platform sockets (POSIX + winsock); no header cipher (trusted local
// link, distinct from the game opcode stream).
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "editor_bridge.hpp"   // EditorFrame, readFrame

namespace wf::editor {

class BridgeClient {
public:
    BridgeClient() = default;
    ~BridgeClient();
    BridgeClient(const BridgeClient&)            = delete;
    BridgeClient& operator=(const BridgeClient&) = delete;

    // Connect to host:port (host is a numeric IPv4, e.g. "127.0.0.1"). Starts the
    // receive thread on success.
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool connected() const { return fd_ >= 0; }

    // Send a complete framed packet (e.g. wf::encode(WeatherFx{...})). Returns
    // false if not connected or the write failed.
    bool send(const std::vector<uint8_t>& framed);

    // Drain the complete frames received since the last call (thread-safe).
    std::vector<EditorFrame> poll();

    uint32_t sentCount() const { return sent_.load(); }
    uint32_t recvCount() const { return recv_.load(); }

private:
    void recvLoop();

    int                     fd_ = -1;
    std::thread             rx_;
    std::atomic<bool>       running_{false};
    std::mutex              mtx_;
    std::queue<EditorFrame> inbox_;
    std::atomic<uint32_t>   sent_{0}, recv_{0};
};

} // namespace wf::editor
