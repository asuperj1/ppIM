// =============================================================================
//  im_server/src/net/tcp_session.cpp
// =============================================================================
#include "net/tcp_session.hpp"

#include <spdlog/spdlog.h>

#include "core/chat_service.hpp"
#include "im/common/msg_type.hpp"
#include "im/common/tool.hpp"
#include "im_msg.pb.h"

namespace im::server {

TcpSession::TcpSession(asio::ip::tcp::socket socket, std::shared_ptr<ChatService> chat)
    : socket_(std::move(socket)),
      idle_timer_(socket_.get_executor()),
      chat_(std::move(chat)) {
    asio::error_code ec;
    const auto ep = socket_.remote_endpoint(ec);
    if (!ec) {
        remote = ep.address().to_string() + ":" + std::to_string(ep.port());
    }
}

void TcpSession::start() {
    spdlog::debug("[tcp] connected {}", remote);
    doReadHeader();
    doTick();
}

void TcpSession::doReadHeader() {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(header_), [self](const asio::error_code& ec, std::size_t) {
        if (ec) {
            self->shutdown();
            return;
        }

        self->last_active_ = std::chrono::steady_clock::now();
        const std::uint32_t length = im::readU32BE(self->header_.data());

        if (length == 0 || length > im::kMaxBodyBytes) {
            spdlog::warn("[tcp] illegal frame length {} from {}", length, self->remote);
            self->shutdown();
            return;
        }
        self->doReadBody(length);
    });
}

void TcpSession::doReadBody(std::uint32_t length) {
    body_.assign(length, '\0');

    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(body_), [self](const asio::error_code& ec, std::size_t) {
        if (ec) {
            self->shutdown();
            return;
        }
        std::string package = std::move(self->body_);
        self->dispatch(package);
        self->doReadHeader();
    });
}

void TcpSession::dispatch(const std::string& package) {
    im::MsgPackage pkg;
    if (!pkg.ParseFromString(package)) {
        spdlog::warn("[tcp] bad protobuf packet from {}", remote);
        return;
    }
    chat_->onPacket(shared_from_this(), pkg.msg_type(), pkg.body());
}

void TcpSession::sendProto(im::MsgType type, const google::protobuf::Message& msg) {
    if (closed_) {
        return;
    }

    im::MsgPackage pkg;
    pkg.set_msg_type(static_cast<std::uint32_t>(type));
    msg.SerializeToString(pkg.mutable_body());

    enqueue(im::frame(pkg.SerializeAsString()));
}

void TcpSession::enqueue(std::string data) {
    if (closed_) {
        return;
    }
    const bool idle = out_queue_.empty() && !writing_;
    out_queue_.push_back(std::move(data));
    if (idle) {
        doWrite();
    }
}

void TcpSession::doWrite() {
    if (writing_ || out_queue_.empty()) {
        return;
    }

    writing_ = true;
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(out_queue_.front()),
                      [self](const asio::error_code& ec, std::size_t) {
                          self->writing_ = false;
                          if (ec) {
                              self->shutdown();
                              return;
                          }
                          self->out_queue_.pop_front();
                          self->doWrite();
                      });
}

void TcpSession::doTick() {
    auto self = shared_from_this();
    idle_timer_.expires_after(std::chrono::seconds(20));
    idle_timer_.async_wait([self](const asio::error_code& ec) {
        if (ec || self->closed_) {
            return;
        }
        const auto idle = std::chrono::steady_clock::now() - self->last_active_;
        if (idle > std::chrono::seconds(90)) {
            spdlog::info("[tcp] idle timeout: {}", self->remote);
            self->shutdown();
            return;
        }
        self->doTick();
    });
}

void TcpSession::shutdown() {
    if (closed_) {
        return;
    }
    closed_ = true;

    asio::error_code ec;
    idle_timer_.cancel(ec);
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (!notified_close_) {
        notified_close_ = true;
        chat_->onClose(shared_from_this());
    }
}

void TcpSession::close() {
    shutdown();
}

}  // namespace im::server
