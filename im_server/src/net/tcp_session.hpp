// =============================================================================
//  im_server/src/net/tcp_session.hpp
//  原始 TCP 传输层：4 字节大端长度头 + MsgPackage(protobuf)。
//  这条链路保留给控制台客户端 / 压测工具，浏览器走 WebSocket。
// =============================================================================
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include <asio.hpp>

#include "core/peer.hpp"

namespace im::server {

class ChatService;

class TcpSession : public Peer, public std::enable_shared_from_this<TcpSession> {
public:
    TcpSession(asio::ip::tcp::socket socket, std::shared_ptr<ChatService> chat);

    void start();

    void sendProto(im::MsgType type, const google::protobuf::Message& msg) override;
    void close() override;

private:
    void doReadHeader();
    void doReadBody(std::uint32_t length);
    void dispatch(const std::string& package);

    void enqueue(std::string data);
    void doWrite();

    void doTick();
    void shutdown();

    asio::ip::tcp::socket socket_;
    asio::steady_timer idle_timer_;
    std::shared_ptr<ChatService> chat_;

    std::array<std::uint8_t, 4> header_{};
    std::string body_;

    std::deque<std::string> out_queue_;
    bool writing_{false};
    bool closed_{false};
    bool notified_close_{false};

    std::chrono::steady_clock::time_point last_active_{std::chrono::steady_clock::now()};
};

}  // namespace im::server
