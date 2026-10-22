// =============================================================================
//  im_server/src/web/websocket.hpp
//  RFC 6455 的"够用版"实现：
//    - 服务端握手（Sec-WebSocket-Accept = base64(SHA1(key + GUID))）
//    - 服务端 -> 客户端的帧编码（不掩码）
//    - 客户端 -> 服务端的掩码帧由 HttpSession 负责解掩码
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace im::web {

class WebSocket {
public:
    /// 计算握手响应里的 Sec-WebSocket-Accept
    static std::string acceptKey(std::string_view client_key);

    /// 编码一个 WS 帧（服务端发出的帧不掩码）
    static std::string encodeFrame(std::uint8_t opcode, std::string_view payload, bool fin = true);

    static std::string encodeText(std::string_view text);
    static std::string encodePong(std::string_view payload);
    static std::string encodeClose(std::uint16_t code = 1000, std::string_view reason = {});

    // 常用 opcode
    static constexpr std::uint8_t kContinuation = 0x0;
    static constexpr std::uint8_t kText = 0x1;
    static constexpr std::uint8_t kBinary = 0x2;
    static constexpr std::uint8_t kClose = 0x8;
    static constexpr std::uint8_t kPing = 0x9;
    static constexpr std::uint8_t kPong = 0xA;
};

}  // namespace im::web
