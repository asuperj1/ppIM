// =============================================================================
//  im_server/src/web/http_server.cpp
// =============================================================================
#include "web/http_server.hpp"

#include <spdlog/spdlog.h>

#include "core/chat_service.hpp"
#include "web/http_session.hpp"

namespace im::server {

HttpServer::HttpServer(asio::io_context& io,
                       const asio::ip::tcp::endpoint& endpoint,
                       std::shared_ptr<ChatService> chat,
                       std::string web_root)
    : acceptor_(io),
      endpoint_(endpoint),
      chat_(std::move(chat)),
      web_root_(std::move(web_root)),
      endpoint_text_(endpoint.address().to_string() + ":" + std::to_string(endpoint.port())) {}

bool HttpServer::start(std::string* err) {
    asio::error_code ec;
    acceptor_.open(endpoint_.protocol(), ec);
    if (ec) {
        if (err) *err = "open failed: " + ec.message();
        return false;
    }

    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);

    acceptor_.bind(endpoint_, ec);
    if (ec) {
        if (err) *err = "bind " + endpoint_text_ + " failed: " + ec.message();
        return false;
    }

    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        if (err) *err = "listen failed: " + ec.message();
        return false;
    }

    doAccept();
    spdlog::info("[http] listening on http://{}/", endpoint_text_);
    return true;
}

void HttpServer::doAccept() {
    acceptor_.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            std::make_shared<HttpSession>(std::move(socket), chat_, web_root_)->start();
        } else if (ec != asio::error::operation_aborted) {
            spdlog::warn("[http] accept failed: {}", ec.message());
        }

        if (acceptor_.is_open()) {
            doAccept();
        }
    });
}

}  // namespace im::server
