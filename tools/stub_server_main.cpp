// wforge-stub-server: run the standalone WorldForge bridge server so the real
// editor (WFORGE_EDITOR_APP) can connect to 127.0.0.1:7878 and exercise the full
// round-trip with no mangos-zero -- ops mutate an in-memory WorldSim and the
// server streams debug back. A smoke target before dropping the bridge into a
// real server.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "server/stub_server.hpp"

int main(int argc, char** argv) {
    uint16_t port = (argc > 1) ? (uint16_t)std::atoi(argv[1]) : 7878;
    wf::StubBridgeServer server;
    if (!server.start(port)) { std::fprintf(stderr, "failed to bind :%u\n", port); return 1; }
    std::printf("WorldForge stub bridge listening on 127.0.0.1:%u (Ctrl-C to stop)\n", server.port());
    std::fflush(stdout);
    while (server.running()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
