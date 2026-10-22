// =============================================================================
//  im_server/src/web/http_server.hpp
//  监听 8080：既发静态网页，也承载 /ws 的 WebSocket 升级。
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include <asio.hpp>

namespace im::server {

class ChatService;

class HttpServer {
public:
    HttpServer(asio::io_context& io,
               const asio::ip::tcp::endpoint& endpoint,
               std::shared_ptr<ChatService> chat,
               std::string web_root);

    bool start(std::string* err);

private:
    void doAccept();

    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::endpoint endpoint_;
    std::shared_ptr<ChatService> chat_;
    std::string web_root_;
    std::string endpoint_text_;
};

}  // namespace im::server
