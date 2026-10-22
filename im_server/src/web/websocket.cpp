// =============================================================================
//  im_server/src/web/websocket.cpp
// =============================================================================
#include "web/websocket.hpp"

#include <string>

#include "im/common/crypto.hpp"

namespace im::web {
namespace {

// RFC 6455 规定的握手魔法字符串
constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

}  // namespace

std::string WebSocket::acceptKey(std::string_view client_key) {
    std::string data(client_key);
    data.append(kGuid);
    return im::crypto::base64(im::crypto::sha1Raw(data));
}

std::string WebSocket::encodeFrame(std::uint8_t opcode, std::string_view payload, bool fin) {
    std::string frame;
    frame.reserve(payload.size() + 10);

    frame.push_back(static_cast<char>((fin ? 0x80u : 0x00u) | (opcode & 0x0Fu)));

    const std::size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFFu));
        frame.push_back(static_cast<char>(len & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((static_cast<std::uint64_t>(len) >> (i * 8)) & 0xFFu));
        }
    }

    frame.append(payload);
    return frame;
}

std::string WebSocket::encodeText(std::string_view text) {
    return encodeFrame(kText, text);
}

std::string WebSocket::encodePong(std::string_view payload) {
    return encodeFrame(kPong, payload);
}

std::string WebSocket::encodeClose(std::uint16_t code, std::string_view reason) {
    std::string payload;
    payload.push_back(static_cast<char>((code >> 8) & 0xFFu));
    payload.push_back(static_cast<char>(code & 0xFFu));
    payload.append(reason);
    return encodeFrame(kClose, payload);
}

}  // namespace im::web
