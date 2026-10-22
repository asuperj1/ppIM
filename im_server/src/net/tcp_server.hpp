// =============================================================================
//  im_server/src/net/tcp_server.hpp
//  原始 TCP 监听（默认 9000），保留协议兼容性。
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include <asio.hpp>

namespace im::server {

class ChatService;

class TcpServer {
public:
    TcpServer(asio::io_context& io,
              const asio::ip::tcp::endpoint& endpoint,
              std::shared_ptr<ChatService> chat);

    bool start(std::string* err);

private:
    void doAccept();

    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::endpoint endpoint_;
    std::shared_ptr<ChatService> chat_;
    std::string endpoint_text_;
};

}  // namespace im::server
