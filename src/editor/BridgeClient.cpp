#include "editor/BridgeClient.hpp"

#include <chrono>
#include <cstring>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using ssize_t = long long;
  #define WF_CLOSESOCK closesocket
  #define WF_SENDFLAGS 0
  static void wfNetInit() { static bool once = []{ WSADATA w; WSAStartup(MAKEWORD(2,2), &w); return true; }(); (void)once; }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define WF_CLOSESOCK ::close
  #ifdef MSG_NOSIGNAL
    #define WF_SENDFLAGS MSG_NOSIGNAL
  #else
    #define WF_SENDFLAGS 0
  #endif
  static void wfNetInit() {}
#endif

namespace wf::editor {

BridgeClient::~BridgeClient() { disconnect(); }

bool BridgeClient::connect(const std::string& host, uint16_t port) {
    wfNetInit();
    disconnect();

    int fd = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) { WF_CLOSESOCK(fd); return false; }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        WF_CLOSESOCK(fd); return false;
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));

    fd_ = fd;
    running_ = true;
    rx_ = std::thread([this]{ recvLoop(); });
    return true;
}

void BridgeClient::disconnect() {
    running_ = false;
    if (fd_ >= 0) {
#if defined(_WIN32)
        ::shutdown(fd_, SD_BOTH);
#else
        ::shutdown(fd_, SHUT_RDWR);
#endif
    }
    if (rx_.joinable()) rx_.join();
    if (fd_ >= 0) { WF_CLOSESOCK(fd_); fd_ = -1; }
}

bool BridgeClient::send(const std::vector<uint8_t>& framed) {
    if (fd_ < 0) return false;
    size_t off = 0;
    while (off < framed.size()) {
        ssize_t n = ::send(fd_, reinterpret_cast<const char*>(framed.data() + off),
                           (int)(framed.size() - off), WF_SENDFLAGS);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    ++sent_;
    return true;
}

std::vector<EditorFrame> BridgeClient::poll() {
    std::vector<EditorFrame> out;
    std::lock_guard<std::mutex> g(mtx_);
    out.swap(deferred_);                    // frames waitForAck set aside first
    while (!inbox_.empty()) { out.push_back(std::move(inbox_.front())); inbox_.pop(); }
    // Any ack passing through resolves its pending entry (5 = u32 opId + u8 status).
    for (const EditorFrame& f : out)
        if (f.opcode == EDITOR_ACK && f.payload.size() >= 5)
            pending_.erase(decodeAck(f.payload).opId);
    return out;
}

// ---- op send helpers ----
uint32_t BridgeClient::trackedSend(const std::vector<uint8_t>& framed, uint32_t opId) {
    { std::lock_guard<std::mutex> g(mtx_); pending_.insert(opId); }
    if (!send(framed)) {
        std::lock_guard<std::mutex> g(mtx_);
        pending_.erase(opId);
        return 0;
    }
    return opId;
}

uint32_t BridgeClient::sendReloadGrid(uint32_t mapId, int32_t gx, int32_t gy) {
    ReloadGrid op; op.mapId = mapId; op.gx = gx; op.gy = gy; op.opId = nextOpId_++;
    return trackedSend(encode(op), op.opId);
}

uint32_t BridgeClient::sendMarkPoints(const std::vector<Vec3>& points, uint32_t ttlMs) {
    MarkPoints op; op.points = points; op.ttlMs = ttlMs; op.opId = nextOpId_++;
    return trackedSend(encode(op), op.opId);
}

uint32_t BridgeClient::sendSqlApply(const std::string& sql, const std::string& reloadCommand) {
    // The frame size field is a u16 counting opcode + payload; an oversize
    // combined payload would wrap it and desync the socket, so reject locally
    // (return 0 = failed) instead of letting the encoder truncate.
    constexpr size_t kMaxPayload = 0xFFFF - 4;               // u16 size - u32 opcode
    const size_t payload = 2 + sql.size() + 2 + reloadCommand.size() + 4;
    if (payload > kMaxPayload) return 0;
    SqlApply op; op.sql = sql; op.reloadCommand = reloadCommand; op.opId = nextOpId_++;
    return trackedSend(encode(op), op.opId);
}

size_t BridgeClient::pendingAckCount() const {
    std::lock_guard<std::mutex> g(mtx_);
    return pending_.size();
}

bool BridgeClient::waitForAck(uint32_t opId, Ack& out, int timeoutMs) {
    const int stepMs = 5;
    for (int waited = 0;; waited += stepMs) {
        bool found = false;
        std::vector<EditorFrame> leftovers;
        for (EditorFrame& f : poll()) {
            if (!found && f.opcode == EDITOR_ACK && f.payload.size() >= 5) {
                Ack a = decodeAck(f.payload);
                if (a.opId == opId) { out = a; found = true; continue; }
            }
            leftovers.push_back(std::move(f));   // not ours: next poll() gets it
        }
        if (!leftovers.empty()) {
            std::lock_guard<std::mutex> g(mtx_);
            deferred_.insert(deferred_.end(),
                             std::make_move_iterator(leftovers.begin()),
                             std::make_move_iterator(leftovers.end()));
        }
        if (found) return true;
        if (waited >= timeoutMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
    }
}

BridgeClient::ApplyResult BridgeClient::applyChangeset(const Changeset& cs, int ackTimeoutMs) {
    // The frame size field is a uint16 counting opcode + payload, so one
    // statement caps out just under 64 KiB; reject locally instead of letting
    // the string encoder truncate live SQL.
    constexpr size_t kMaxSqlBytes = 60000;

    ApplyResult res;
    const std::vector<ChangeEntry>& entries = cs.entries();
    auto shipAckGated = [&](const std::string& sql, const std::string& reload,
                            size_t reportIndex) {
        if (sql.size() > kMaxSqlBytes || reload.size() > kMaxSqlBytes) {
            res.failedIndex = reportIndex; res.ackStatus = kStatusTooLarge;
            return false;
        }
        uint32_t opId = sendSqlApply(sql, reload);
        Ack ack;
        if (opId == 0 || !waitForAck(opId, ack, ackTimeoutMs)) {
            res.failedIndex = reportIndex; res.timedOut = true;
            return false;
        }
        if (ack.status != 0) {
            res.failedIndex = reportIndex; res.ackStatus = ack.status;
            return false;
        }
        return true;
    };

    for (size_t i = 0; i < entries.size(); ++i) {
        if (!shipAckGated(entries[i].sql, /*reload*/{}, i)) return res;
        ++res.applied;
    }
    std::vector<std::string> reloads = cs.reloadCommands();
    for (size_t i = 0; i < reloads.size(); ++i)
        if (!shipAckGated(/*sql*/{}, reloads[i], entries.size() + i)) return res;

    res.ok = true;
    return res;
}

void BridgeClient::recvLoop() {
    std::vector<uint8_t> acc;
    uint8_t buf[4096];
    while (running_) {
        ssize_t n = ::recv(fd_, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n <= 0) break;                       // peer closed or error
        acc.insert(acc.end(), buf, buf + (size_t)n);

        // Pull every complete frame out of the front of the accumulator.
        for (;;) {
            EditorFrame f; size_t consumed = 0;
            if (!readFrame(acc, f, consumed)) break;   // need more bytes
            {
                std::lock_guard<std::mutex> g(mtx_);
                inbox_.push(std::move(f));
            }
            ++recv_;
            acc.erase(acc.begin(), acc.begin() + consumed);
        }
    }
    running_ = false;
}

} // namespace wf::editor
