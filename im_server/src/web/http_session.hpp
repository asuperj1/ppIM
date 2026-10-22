// =============================================================================
//  im_server/src/web/http_session.hpp
//  一条浏览器连接的生命周期：
//      1) 先当 HTTP 用：读到 \r\n\r\n 为止
//      2) 如果是 GET /ws + Upgrade: websocket  -> 101 握手，转成 WebSocket
//      3) 否则当静态文件服务器：把 web/ 目录里的文件读出来回过去，然后关闭
//
//  WebSocket 消息有两种帧：
//      - 文本帧：WebEnvelope JSON（浏览器用这个）
//      - 二进制帧：MsgPackage protobuf（想复用原协议的客户端用这个）
// =============================================================================
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "core/peer.hpp"

namespace im::server {

class ChatService;

class HttpSession : public Peer, public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(asio::ip::tcp::socket socket,
                std::shared_ptr<ChatService> chat,
                std::string web_root);

    void start();

    void sendProto(im::MsgType type, const google::protobuf::Message& msg) override;
    void close() override;

private:
    // ---- HTTP ----
    void doReadRequest();
    void handleRequest(const std::string& head, const std::string& body);
    // 处理 JSON / OAuth 接口；返回 true 表示已响应（调用方不要再走静态文件）
    bool handleApi(const std::string& method,
                   const std::string& path,
                   const std::string& query,
                   const std::unordered_map<std::string, std::string>& headers,
                   const std::string& body);
    void doUpgrade(std::string_view client_key);
    void respondAndClose(int status, std::string_view reason,
                         std::string_view content_type, std::string_view body,
                         bool head_only = false);

    // ---- WebSocket ----
    void doReadWsHeader();
    void doReadWsExtendedLength(std::size_t bytes, bool masked);
    void doReadWsMask(bool masked, std::uint64_t length);
    void doReadWsPayload(bool masked, std::uint64_t length);
    void handleWsFrame(std::uint8_t opcode, std::string payload, bool fin);
    void dispatchText(const std::string& text);
    void dispatchBinary(const std::string& data);

    // ---- 输出队列 ----
    void enqueue(std::string data);
    void doWrite();

    // ---- 生命周期 / 心跳 ----
    void doTick();
    void shutdown();

    asio::ip::tcp::socket socket_;
    asio::steady_timer idle_timer_;
    std::shared_ptr<ChatService> chat_;
    std::string web_root_;

    std::array<char, 8192> recv_buf_{};
    std::string http_buf_;

    std::deque<std::string> out_queue_;
    bool writing_{false};
    bool close_after_write_{false};
    bool closed_{false};

    bool websocket_{false};
    bool notified_close_{false};

    std::array<std::uint8_t, 2> ws_header_{};
    std::array<std::uint8_t, 8> ws_ext_{};
    std::array<std::uint8_t, 4> ws_mask_{};
    std::string ws_payload_;
    std::string ws_fragment_;
    std::uint8_t ws_opcode_{0};
    std::uint8_t ws_fragment_opcode_{0};
    bool ws_fin_{true};
    bool ws_fragment_active_{false};

    std::chrono::steady_clock::time_point last_active_{std::chrono::steady_clock::now()};
};

}  // namespace im::server
