#include "test.hpp"
#include "editor/BridgeClient.hpp"
#include "editor_bridge.hpp"
#include "fxbridge.hpp"

#include <chrono>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace wf;
using namespace wf::editor;

void test_bridge_client() {
    std::printf("[editor.bridge_client]\n");

#if defined(_WIN32)
    std::printf("  (loopback test skipped on Windows)\n");
    CHECK(true);
    return;
#else
    // --- a minimal loopback "server": accept, read one frame, echo an Ack ----
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(srv >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;                                  // ephemeral port
    CHECK(::bind(srv, (sockaddr*)&addr, sizeof(addr)) == 0);
    CHECK(::listen(srv, 1) == 0);
    socklen_t alen = sizeof(addr);
    ::getsockname(srv, (sockaddr*)&addr, &alen);
    uint16_t port = ntohs(addr.sin_port);

    uint32_t serverSawOpcode = 0;
    std::thread server([&]{
        int c = ::accept(srv, nullptr, nullptr);
        if (c < 0) return;
        std::vector<uint8_t> acc;
        uint8_t buf[1024];
        // Read until we can decode one frame.
        for (int tries = 0; tries < 200; ++tries) {
            EditorFrame f; size_t consumed = 0;
            if (readFrame(acc, f, consumed)) {
                serverSawOpcode = f.opcode;
                std::vector<uint8_t> ack = encode(Ack{ /*opId*/ 42, /*status*/ 0 });
                ::send(c, ack.data(), ack.size(), 0);
                break;
            }
            ssize_t n = ::recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            acc.insert(acc.end(), buf, buf + n);
        }
        ::close(c);
    });

    // --- the editor client connects, sends a weather op, awaits the ack ------
    BridgeClient client;
    CHECK(client.connect("127.0.0.1", port));
    CHECK(client.connected());

    WeatherFx w; w.type = WeatherType::Rain; w.grade = 0.6f;
    w.target.scope = FxScope::Zone; w.target.zoneId = 12; w.opId = 7;
    CHECK(client.send(encode(w)));
    CHECK(client.sentCount() == 1);

    // Poll for the ack (bounded wait; no busy spin).
    EditorFrame got; bool gotAck = false;
    for (int i = 0; i < 100 && !gotAck; ++i) {
        for (const EditorFrame& f : client.poll())
            if (f.opcode == EDITOR_ACK) { got = f; gotAck = true; }
        if (!gotAck) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(gotAck);
    if (gotAck) {
        Ack a = decodeAck(got.payload);
        CHECK(a.opId == 42 && a.status == 0);
    }
    CHECK(serverSawOpcode == EDITOR_FX_WEATHER);   // the server received our op

    client.disconnect();
    CHECK(!client.connected());
    server.join();
    ::close(srv);
#endif
}
