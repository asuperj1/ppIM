// =============================================================================
//  im_server/src/core/peer.hpp
//  传输层与业务层之间的唯一接口。
//
//  为什么要抽象 Peer？
//    同一个业务层要同时服务两种连接：
//      - HttpSession（WebSocket，浏览器）
//      - TcpSession （原始 TCP + protobuf，控制台客户端）
//    业务层只认 "把这条 protobuf 消息发出去"，不关心底下是 WS 帧还是长度头帧。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "im/common/msg_type.hpp"

namespace google::protobuf {
class Message;
}

namespace im::server {

class Peer {
public:
    virtual ~Peer() = default;

    /// 发送一条业务消息；具体编码由传输层决定
    virtual void sendProto(im::MsgType type, const google::protobuf::Message& msg) = 0;

    /// 主动断开连接（异步）
    virtual void close() = 0;

    std::uint64_t uid{0};        ///< 登录成功前为 0
    std::string username;
    std::string nickname;
    std::string pp_id;
    std::string remote;          ///< 对端地址，仅用于日志

    bool isLoggedIn() const noexcept { return uid != 0; }
};

}  // namespace im::server
