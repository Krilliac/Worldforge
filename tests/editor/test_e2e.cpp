#include "test.hpp"
#include "server/stub_server.hpp"
#include "editor/BridgeClient.hpp"
#include "editor_bridge.hpp"
#include "fxbridge.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace wf;
using namespace wf::editor;

namespace {
// Spin until `pred()` holds or the deadline passes (no busy-burn).
template <class F>
bool waitFor(F pred, int maxMs = 2000) {
    for (int i = 0; i < maxMs / 10 && !pred(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return pred();
}
} // namespace

void test_e2e() {
    std::printf("[e2e]\n");

    // The whole loop: a standalone bridge server + the editor's BridgeClient.
    StubBridgeServer server;
    CHECK(server.start(0));               // ephemeral port
    CHECK(server.port() != 0);

    BridgeClient client;
    CHECK(client.connect("127.0.0.1", server.port()));

    // 1) spawn a creature -> the world gains an object, and the server streams a
    //    debug marker back to the editor.
    SpawnCreature sp; sp.entry = 299; sp.mapId = 0; sp.pos = {-9449.0f, 64.0f, 56.0f}; sp.opId = 1;
    CHECK(client.send(encode(sp)));
    CHECK(waitFor([&]{ return server.aliveCount() == 1; }));

    auto gids = server.guids();
    CHECK(gids.size() == 1);
    uint64_t guid = gids[0];

    // 2) move it -> the world updates the position.
    MoveObject mv; mv.guid = guid; mv.pos = {-9440.0f, 70.0f, 56.5f}; mv.opId = 2;
    CHECK(client.send(encode(mv)));
    CHECK(waitFor([&]{
        SimObject o; return server.getObject(guid, o) && std::fabs(o.pos.y - 70.0f) < 0.01f;
    }));

    // 3) set a patrol -> the world stores it and the server streams the path back.
    SetWaypoints wp; wp.guid = guid; wp.opId = 3;
    wp.path = { {-9440,70,56}, {-9430,75,56}, {-9420,70,56} };
    CHECK(client.send(encode(wp)));
    CHECK(waitFor([&]{
        SimObject o; return server.getObject(guid, o) && o.waypoints.size() == 3;
    }));

    // 4) an atmosphere op -> the server logs the weather broadcast.
    WeatherFx wx; wx.type = WeatherType::Rain; wx.grade = 0.8f;
    wx.target.scope = FxScope::Zone; wx.target.zoneId = 12; wx.opId = 4;
    CHECK(client.send(encode(wx)));
    CHECK(waitFor([&]{ return server.fxSnapshot().weatherCount == 1; }));

    // 5) despawn -> the world is empty again.
    Despawn dp; dp.guid = guid; dp.opId = 5;
    CHECK(client.send(encode(dp)));
    CHECK(waitFor([&]{ return server.aliveCount() == 0; }));

    // The editor received acks for all five ops + the streamed debug frames.
    int acks = 0, debugFrames = 0;
    bool gotMarker = false, gotPath = false;
    waitFor([&]{
        for (const EditorFrame& f : client.poll()) {
            if (f.opcode == EDITOR_ACK) ++acks;
            else { ++debugFrames;
                   if (f.opcode == EDITOR_DEBUG_MARKER) gotMarker = true;
                   if (f.opcode == EDITOR_DEBUG_PATH)   gotPath = true; }
        }
        return acks >= 5;
    });
    CHECK(acks >= 5);
    CHECK(gotMarker);                     // spawn streamed a marker
    CHECK(gotPath);                       // waypoints streamed a path
    CHECK(debugFrames >= 2);

    client.disconnect();
    server.stop();
    CHECK(!server.running());
}
