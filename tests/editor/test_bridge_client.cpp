#include "test.hpp"
#include "editor/BridgeClient.hpp"
#include "changeset.hpp"
#include "editor_bridge.hpp"
#include "fxbridge.hpp"
#include "server/world_sim.hpp"

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
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

using namespace wf;
using namespace wf::editor;

namespace {

// A minimal one-connection loopback server end: accept, decode frames, apply
// the ops WorldSim owns (RELOAD_GRID / MARK_POINTS / SQL_APPLY via
// handleEditorFrame -- the "caller dispatches the rest" contract), ack every
// op. `failNthSql` (0-based) error-acks that SQL_APPLY without applying it, so
// the client's ack-gated paths can be proven. The session thread owns `sim`
// until join(); inspect it after.
class LoopbackServer {
public:
    ~LoopbackServer() { join(); }

    bool start(int failNthSql = -1) {
        wfNetInit();
        failNthSql_ = failNthSql;

        srv_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
        if (srv_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;                                   // ephemeral port
        if (::bind(srv_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (::listen(srv_, 1) != 0) return false;
        socklen_t alen = sizeof(addr);
        ::getsockname(srv_, reinterpret_cast<sockaddr*>(&addr), &alen);
        port_ = ntohs(addr.sin_port);

        th_ = std::thread([this]{ run(); });
        return true;
    }

    // Wait for the session to end (the client disconnecting ends it); the sim
    // and counters are safe to read afterwards.
    void join() {
        if (th_.joinable()) th_.join();
        if (srv_ >= 0) { WF_CLOSESOCK(srv_); srv_ = -1; }
    }

    uint16_t port() const { return port_; }

    WorldSim sim;              // the applied-op logs live here (read after join)
    int      weatherOps = 0;   // raw-send ops outside the sim's ownership

private:
    void run() {
        int c = (int)::accept(srv_, nullptr, nullptr);
        if (c < 0) return;

        std::vector<uint8_t> acc;
        uint8_t buf[4096];
        int sqlSeen = 0;
        for (;;) {
            ssize_t n = ::recv(c, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (n <= 0) break;                     // client disconnected
            acc.insert(acc.end(), buf, buf + (size_t)n);

            for (;;) {
                EditorFrame f; size_t consumed = 0;
                if (!readFrame(acc, f, consumed)) break;

                Ack ack;
                if (f.opcode == EDITOR_SQL_APPLY && sqlSeen++ == failNthSql_) {
                    SqlApply op;                   // injected failure: error-ack,
                    ack.opId = decodeSqlApply(f.payload, op) ? op.opId : 0;
                    ack.status = 1;                // ...apply nothing
                } else if (!sim.handleEditorFrame(f, ack)) {
                    if (f.opcode == EDITOR_FX_WEATHER) {
                        WeatherFx w = decodeWeatherFx(f.payload);
                        ++weatherOps;
                        ack.opId = w.opId;         // ok-ack it by its own opId
                    }
                    // anything else: ack { 0, 0 } (the stub has no opinion)
                }

                std::vector<uint8_t> reply = encode(ack);
                size_t off = 0;
                while (off < reply.size()) {
                    ssize_t s = ::send(c, reinterpret_cast<const char*>(reply.data() + off),
                                       (int)(reply.size() - off), 0);
                    if (s <= 0) break;
                    off += (size_t)s;
                }
                acc.erase(acc.begin(), acc.begin() + consumed);
            }
        }
        WF_CLOSESOCK(c);
    }

    int         srv_ = -1;
    uint16_t    port_ = 0;
    int         failNthSql_ = -1;
    std::thread th_;
};

} // namespace

void test_bridge_client() {
    std::printf("[editor.bridge_client]\n");

    // --- raw send + the op send helpers over a real loopback socket ----------
    {
        LoopbackServer server;
        CHECK(server.start());
        BridgeClient client;
        CHECK(client.connect("127.0.0.1", server.port()));
        CHECK(client.connected());

        // the raw framed path still works (a panel-emitted FX op)
        WeatherFx w; w.type = WeatherType::Rain; w.grade = 0.6f;
        w.target.scope = FxScope::Zone; w.target.zoneId = 12; w.opId = 77;
        CHECK(client.send(encode(w)));
        Ack wa;
        CHECK(client.waitForAck(77, wa));
        CHECK(wa.opId == 77 && wa.status == 0);

        // helpers allocate distinct opIds and track the pending ack until it lands
        uint32_t op1 = client.sendReloadGrid(1, -3, 60);
        CHECK(op1 != 0);
        Ack a1;
        CHECK(client.waitForAck(op1, a1));
        CHECK(a1.opId == op1 && a1.status == 0);
        CHECK(client.pendingAckCount() == 0);

        uint32_t op2 = client.sendMarkPoints({ {1,2,3}, {4,5,6} }, 5000);
        uint32_t op3 = client.sendSqlApply(
            "UPDATE creature_template SET minlevel = 5 WHERE entry = 299;",
            ".reload creature_template");
        CHECK(op2 != 0 && op3 != 0 && op2 != op3);
        Ack a2, a3;
        CHECK(client.waitForAck(op2, a2) && a2.status == 0);
        CHECK(client.waitForAck(op3, a3) && a3.status == 0);
        CHECK(client.pendingAckCount() == 0);

        client.disconnect();
        CHECK(!client.connected());
        server.join();

        // the ops really landed on the server end
        CHECK(server.weatherOps == 1);
        CHECK(server.sim.reloadedGrids().size() == 1);
        CHECK(server.sim.reloadedGrids()[0].mapId == 1);
        CHECK(server.sim.reloadedGrids()[0].gx == -3);
        CHECK(server.sim.reloadedGrids()[0].gy == 60);
        CHECK(server.sim.markers().size() == 2);
        CHECK_APPROX(server.sim.markers()[1].pos.y, 5.0f);
        CHECK(server.sim.appliedSql().size() == 1);
        CHECK(server.sim.appliedSql()[0].reloadCommand == ".reload creature_template");
    }

    // A changeset shared by the apply scenarios: three entries, two tables
    // (creature_template twice -> its reload dedupes to one command).
    Changeset cs("wolf tuning");
    cs.add("UPDATE creature_template SET minlevel = 5 WHERE entry = 299;",
           "raise wolf level", "creature_template");
    cs.add("DELETE FROM creature_loot_template WHERE entry = 299;",
           "clear loot", "creature_loot_template");
    cs.add("UPDATE creature_template SET maxlevel = 6 WHERE entry = 299;",
           "cap wolf level", "creature_template");

    // --- applyChangeset: every entry in order, then the deduped reloads ------
    {
        LoopbackServer server;
        CHECK(server.start());
        BridgeClient client;
        CHECK(client.connect("127.0.0.1", server.port()));

        BridgeClient::ApplyResult res = client.applyChangeset(cs);
        CHECK(res.ok);
        CHECK(res.applied == 3);
        CHECK(res.failedIndex == SIZE_MAX);
        CHECK(res.ackStatus == 0 && !res.timedOut);
        CHECK(client.pendingAckCount() == 0);

        client.disconnect();
        server.join();

        const std::vector<SqlRecord>& log = server.sim.appliedSql();
        CHECK(log.size() == 5);                    // 3 entries + 2 reloads
        CHECK(log[0].sql == cs.entries()[0].sql && log[0].reloadCommand.empty());
        CHECK(log[1].sql == cs.entries()[1].sql);
        CHECK(log[2].sql == cs.entries()[2].sql);
        CHECK(log[3].sql.empty() && log[3].reloadCommand == ".reload creature_loot_template");
        CHECK(log[4].sql.empty() && log[4].reloadCommand == ".reload creature_template");
    }

    // --- applyChangeset is ack-gated: an error ack at entry 2 stops there ----
    {
        LoopbackServer server;
        CHECK(server.start(/*failNthSql*/ 2));
        BridgeClient client;
        CHECK(client.connect("127.0.0.1", server.port()));

        BridgeClient::ApplyResult res = client.applyChangeset(cs);
        CHECK(!res.ok);
        CHECK(res.applied == 2);                   // entries 0 and 1 made it...
        CHECK(res.failedIndex == 2);               // ...then entry 2 error-acked
        CHECK(res.ackStatus == 1);
        CHECK(!res.timedOut);

        client.disconnect();
        server.join();

        const std::vector<SqlRecord>& log = server.sim.appliedSql();
        CHECK(log.size() == 2);                    // entry 2 + reloads never applied
        CHECK(log[0].sql == cs.entries()[0].sql);
        CHECK(log[1].sql == cs.entries()[1].sql);
    }

    // --- an oversized statement is rejected locally, before the wire ---------
    {
        Changeset big("too big");
        big.add(std::string(70000, 'x'), "over the u16 frame cap", "creature_template");

        LoopbackServer server;
        CHECK(server.start());
        BridgeClient client;
        CHECK(client.connect("127.0.0.1", server.port()));

        BridgeClient::ApplyResult res = client.applyChangeset(big);
        CHECK(!res.ok);
        CHECK(res.applied == 0 && res.failedIndex == 0);
        CHECK(res.ackStatus == BridgeClient::kStatusTooLarge);

        client.disconnect();
        server.join();
        CHECK(server.sim.appliedSql().empty());
    }
}
