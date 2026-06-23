#include "editor/BridgeClient.hpp"

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
    while (!inbox_.empty()) { out.push_back(std::move(inbox_.front())); inbox_.pop(); }
    return out;
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
