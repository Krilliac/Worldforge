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
#include <unordered_set>
#include <vector>

#include "changeset.hpp"       // Changeset (applyChangeset)
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
    // Acks passing through here also clear the matching pending-ack entry.
    std::vector<EditorFrame> poll();

    uint32_t sentCount() const { return sent_.load(); }
    uint32_t recvCount() const { return recv_.load(); }

    // ---- op send helpers: allocate an opId, frame, send, track the pending
    //      ack. Each returns the allocated opId, or 0 if the send failed. ----
    uint32_t sendReloadGrid(uint32_t mapId, int32_t gx, int32_t gy);
    uint32_t sendMarkPoints(const std::vector<Vec3>& points, uint32_t ttlMs);
    uint32_t sendSqlApply(const std::string& sql, const std::string& reloadCommand);

    // Poll until the ack for `opId` arrives (bounded wait, no busy spin).
    // Frames that are not that ack are kept and returned by the next poll().
    // False on timeout.
    bool waitForAck(uint32_t opId, Ack& out, int timeoutMs = 2000);

    // Helper-sent ops still awaiting their ack (poll()/waitForAck clear them).
    size_t pendingAckCount() const;

    // Outcome of applyChangeset: how far it got and why it stopped.
    struct ApplyResult {
        bool    ok          = false;
        size_t  applied     = 0;        // entries acked ok before any failure
        size_t  failedIndex = SIZE_MAX; // failing entry index; a failing reload
                                        // command reports entries.size() + idx
        uint8_t ackStatus   = 0;        // the server's error-ack status (0 if none)
        bool    timedOut    = false;    // failed by missing ack, not an error ack
    };

    // Ship every entry as EDITOR_SQL_APPLY, sequential and ack-gated (stop at
    // the first error ack or timeout), then the deduped reload commands the
    // same way. Entries whose SQL exceeds the ~64 KiB frame limit fail locally
    // (ackStatus kStatusTooLarge) rather than being truncated on the wire.
    ApplyResult applyChangeset(const Changeset& cs, int ackTimeoutMs = 2000);

    static constexpr uint8_t kStatusTooLarge = 0xFF;   // local reject, never sent

private:
    void recvLoop();
    // Insert `opId` into the pending set, then send; on failure un-track and
    // return 0. The shared tail of the send helpers.
    uint32_t trackedSend(const std::vector<uint8_t>& framed, uint32_t opId);

    int                      fd_ = -1;
    std::thread              rx_;
    std::atomic<bool>        running_{false};
    mutable std::mutex       mtx_;
    std::queue<EditorFrame>  inbox_;
    std::vector<EditorFrame> deferred_;     // set aside by waitForAck (mtx_)
    std::unordered_set<uint32_t> pending_;  // opIds awaiting an ack (mtx_)
    std::atomic<uint32_t>    nextOpId_{1};
    std::atomic<uint32_t>    sent_{0}, recv_{0};
};

} // namespace wf::editor
