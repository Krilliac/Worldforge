#include "server/stub_server.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "editor_bridge.hpp"
#include "fxbridge.hpp"

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using ssize_t = long long;
  #define WF_CLOSESOCK closesocket
  static void wfNetInit() { static bool once = []{ WSADATA w; WSAStartup(MAKEWORD(2,2), &w); return true; }(); (void)once; }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define WF_CLOSESOCK ::close
  static void wfNetInit() {}
#endif

namespace wf {

namespace {
bool sendAll(int fd, const std::vector<uint8_t>& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = ::send(fd, reinterpret_cast<const char*>(bytes.data() + off),
                           (int)(bytes.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}
} // namespace

StubBridgeServer::~StubBridgeServer() { stop(); }

bool StubBridgeServer::start(uint16_t port) {
    wfNetInit();
    stop();

    int fd = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { WF_CLOSESOCK(fd); return false; }
    if (::listen(fd, 4) != 0) { WF_CLOSESOCK(fd); return false; }

    socklen_t alen = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen);
    port_ = ntohs(addr.sin_port);

    listenFd_ = fd;
    running_ = true;
    acceptThread_ = std::thread([this]{ acceptLoop(); });
    if (simRateHz_ > 0.0) simThread_ = std::thread([this]{ simLoop(); });
    return true;
}

void StubBridgeServer::stop() {
    running_ = false;
    if (listenFd_ >= 0) {
#if defined(_WIN32)
        ::shutdown(listenFd_, SD_BOTH);
#else
        ::shutdown(listenFd_, SHUT_RDWR);
#endif
        WF_CLOSESOCK(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    if (simThread_.joinable())    simThread_.join();
}

uint64_t StubBridgeServer::simTimeMs() {
    std::lock_guard<std::mutex> g(worldMtx_); return world_.simTimeMs();
}

// --- client registry + serialised sends ---
void StubBridgeServer::addClient(int fd) {
    std::lock_guard<std::mutex> g(clientsMtx_); clients_.push_back(fd);
}
void StubBridgeServer::removeClient(int fd) {
    std::lock_guard<std::mutex> g(clientsMtx_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end());
}
bool StubBridgeServer::sendFramed(int fd, const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> g(sendMtx_);   // no interleaving with the broadcast
    return sendAll(fd, bytes);
}
void StubBridgeServer::broadcast(const std::vector<uint8_t>& bytes) {
    std::vector<int> targets;
    { std::lock_guard<std::mutex> g(clientsMtx_); targets = clients_; }
    std::lock_guard<std::mutex> g(sendMtx_);
    for (int fd : targets) sendAll(fd, bytes);
}

// --- simulation thread: tick the world, stream live state to every client ---
void StubBridgeServer::simLoop() {
    using clock = std::chrono::steady_clock;
    const double dt = 1.0 / simRateHz_;
    const auto   period = std::chrono::duration_cast<clock::duration>(
                              std::chrono::duration<double>(dt));
    std::vector<uint64_t> prevGuids;

    auto last = clock::now();
    while (running_) {
        std::this_thread::sleep_until(last + period);
        last = clock::now();

        std::vector<SimObject> snap;
        {
            std::lock_guard<std::mutex> g(worldMtx_);
            world_.tick(static_cast<float>(dt));
            snap = world_.snapshot();
        }

        // Stream every object's live state.
        std::vector<uint64_t> nowGuids;
        nowGuids.reserve(snap.size());
        for (const SimObject& o : snap) {
            nowGuids.push_back(o.guid);
            EntityState e;
            e.guid = o.guid; e.kind = static_cast<uint8_t>(o.kind);
            e.entry = o.entry; e.mapId = o.mapId;
            e.pos = o.pos; e.orientation = o.orientation;
            e.moving = o.moving; e.speed = o.speed; e.name = o.name;
            broadcast(encode(e));
        }
        // Retire objects that left the world since last tick.
        for (uint64_t g : prevGuids)
            if (std::find(nowGuids.begin(), nowGuids.end(), g) == nowGuids.end())
                broadcast(encode(EntityRemove{ g }));
        prevGuids.swap(nowGuids);
    }
}

void StubBridgeServer::acceptLoop() {
    while (running_) {
        int c = (int)::accept(listenFd_, nullptr, nullptr);
        if (c < 0) break;                       // listen socket closed
        clientLoop(c);                          // one client at a time (stub)
        WF_CLOSESOCK(c);
    }
}

void StubBridgeServer::clientLoop(int fd) {
    addClient(fd);
    std::vector<uint8_t> acc;
    uint8_t buf[4096];
    while (running_) {
        ssize_t n = ::recv(fd, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n <= 0) break;
        acc.insert(acc.end(), buf, buf + (size_t)n);

        for (;;) {
            EditorFrame f; size_t consumed = 0;
            if (!readFrame(acc, f, consumed)) break;
            std::vector<uint8_t> reply;          // optional debug stream back

            uint32_t opId = 0; uint8_t status = 0;
            {
                std::lock_guard<std::mutex> g(worldMtx_);
                switch (f.opcode) {
                    case EDITOR_HELLO: break;
                    case EDITOR_MOVE_OBJECT: {
                        auto o = decodeMoveObject(f.payload); opId = o.opId;
                        status = world_.moveObject(o.guid, o.pos, o.orientation) ? 0 : 1;
                        break;
                    }
                    case EDITOR_SPAWN_CREATURE: {
                        auto o = decodeSpawnCreature(f.payload); opId = o.opId;
                        uint64_t guid = world_.spawnCreature(o.entry, o.mapId, o.pos, o.orientation);
                        (void)guid;
                        // Echo a debug marker at the spawn so the editor sees it.
                        DebugMarker m; m.type = DebugVisType::Generic; m.pos = o.pos;
                        m.color = {0,255,0,255}; m.value = (float)o.entry; m.label = "spawn";
                        reply = encode(m);
                        break;
                    }
                    case EDITOR_DESPAWN: {
                        auto o = decodeDespawn(f.payload); opId = o.opId;
                        status = world_.despawn(o.guid) ? 0 : 1;
                        break;
                    }
                    case EDITOR_SET_WAYPOINTS: {
                        auto o = decodeSetWaypoints(f.payload); opId = o.opId;
                        status = world_.setWaypoints(o.guid, o.path) ? 0 : 1;
                        DebugPath p; p.guid = o.guid; p.bad = false;
                        p.color = {0,200,255,255}; p.points = o.path;
                        reply = encode(p);                       // stream the path back
                        break;
                    }
                    case EDITOR_OVERRIDE_LIGHT: {
                        auto o = decodeOverrideLight(f.payload); opId = o.opId;
                        world_.fx().lightCount++; world_.fx().lastOverrideLight = o.overrideLightId;
                        break;
                    }
                    case EDITOR_FX_WEATHER: {
                        auto o = decodeWeatherFx(f.payload); opId = o.opId;
                        world_.fx().weatherCount++;
                        world_.fx().weatherType = (uint32_t)o.type; world_.fx().weatherGrade = o.grade;
                        break;
                    }
                    case EDITOR_FX_SOUND: {
                        auto o = decodeSoundFx(f.payload); opId = o.opId;
                        world_.fx().soundCount++; world_.fx().lastSound = o.soundId; break;
                    }
                    case EDITOR_FX_CINEMATIC: {
                        auto o = decodeCinematicFx(f.payload); opId = o.opId;
                        world_.fx().cinematicCount++; world_.fx().lastCinematic = o.cinematicId; break;
                    }
                    case EDITOR_FX_WORLDSTATE: {
                        auto o = decodeWorldStateFx(f.payload); opId = o.opId;
                        world_.fx().worldStateCount++; break;
                    }
                    default: break;
                }
            }

            if (!reply.empty()) sendFramed(fd, reply);
            sendFramed(fd, encode(Ack{ opId, status }));   // ack every op
            acc.erase(acc.begin(), acc.begin() + consumed);
        }
    }
    removeClient(fd);
}

// ---- thread-safe snapshots ----
size_t StubBridgeServer::aliveCount() {
    std::lock_guard<std::mutex> g(worldMtx_); return world_.aliveCount();
}
bool StubBridgeServer::getObject(uint64_t guid, SimObject& out) {
    std::lock_guard<std::mutex> g(worldMtx_);
    const SimObject* o = world_.find(guid);
    if (!o) return false; out = *o; return true;
}
std::vector<uint64_t> StubBridgeServer::guids() {
    std::lock_guard<std::mutex> g(worldMtx_); return world_.guids();
}
FxLog StubBridgeServer::fxSnapshot() {
    std::lock_guard<std::mutex> g(worldMtx_); return world_.fx();
}

} // namespace wf
