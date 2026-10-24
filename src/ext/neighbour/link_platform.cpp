// ============================================================================
// src/ext/neighbour/link_platform.cpp -- NOT RETAIL. The Link's real transport
// ============================================================================
// link.h's Transport over the Platform WebSocket seam (platform_websocket.h). Not in the
// unit tests (they use a fake transport).
// ============================================================================

#include "ext/neighbour/neighbour.h"

#include "ext/neighbour/link.h"
#include "platform/common/platform_websocket.h"

namespace Neighbour {

namespace {

constexpr uint32_t kConnectTimeoutMs = 8000;

class PlatformTransport final : public Transport {
public:
    ~PlatformTransport() override { Platform::WebSocketFree(ws_); }

    bool Connect(const std::string& url, std::string& error) override {
        ws_ = Platform::WebSocketConnect(url.c_str(), kConnectTimeoutMs, error);
        return ws_ != nullptr;
    }
    bool Send(const std::string& text) override {
        return Platform::WebSocketSendText(ws_, text.data(), text.size());
    }
    Recv Receive(std::string& out) override {
        switch (Platform::WebSocketReceiveText(ws_, out, kMaxMessageBytes)) {
        case Platform::WebSocketRecv::Text:   return Recv::Text;
        case Platform::WebSocketRecv::Closed: return Recv::Closed;
        case Platform::WebSocketRecv::Error:  return Recv::Error;
        }
        return Recv::Error;
    }
    void Close() override { Platform::WebSocketCancel(ws_); }

private:
    Platform::WebSocketHandle ws_ = nullptr;
};

}  // namespace

std::unique_ptr<Transport> MakePlatformTransport() {
    return std::unique_ptr<Transport>(new PlatformTransport());
}

}  // namespace Neighbour
