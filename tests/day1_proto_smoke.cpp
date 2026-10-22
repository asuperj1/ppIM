// =============================================================================
//  tests/day1_proto_smoke.cpp
//  Day1 验收程序：一次性验证三件事
//    1. protobuf 代码生成成功（能 include im_msg.pb.h 并构造消息）
//    2. 大端长度头编解码正确（双向互逆）
//    3. 「粘包」能被长度头正确切开 —— 这是 Day2 Buffer 的预告演示
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <string>

#include <google/protobuf/stubs/common.h>

#include "im/common/msg_type.hpp"
#include "im/common/tool.hpp"
#include "im_msg.pb.h"

namespace {

void printHex(const char* title, const std::string& data) {
    std::printf("[%s] %zu bytes\n    %s\n", title, data.size(), im::toHex(data).c_str());
}

/// 构造一条「私聊」消息的完整帧
std::string buildChatFrame(std::uint64_t from, std::uint64_t to, const std::string& content) {
    im::ChatPrivate chat;
    chat.set_from_uid(from);
    chat.set_to_uid(to);
    chat.set_content(content);
    chat.set_timestamp(1730000000);

    im::MsgPackage pkg;
    pkg.set_msg_type(static_cast<std::uint32_t>(im::MsgType::kChatPrivate));
    pkg.set_body(chat.SerializeAsString());

    return im::frame(pkg.SerializeAsString());
}

}  // namespace

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;   // 校验头文件与链接库版本一致

    int failed = 0;

    // -------------------------------------------------------------------------
    // 步骤 1：发送侧 —— 构造 -> 序列化 -> 加 4 字节大端长度头
    // -------------------------------------------------------------------------
    im::LoginReq req;
    req.set_username("alice");
    req.set_password("123456");

    im::MsgPackage pkg;
    pkg.set_msg_type(static_cast<std::uint32_t>(im::MsgType::kLoginReq));
    pkg.set_body(req.SerializeAsString());

    const std::string payload = pkg.SerializeAsString();
    const std::string txFrame = im::frame(payload);
    printHex("TX whole frame", txFrame);

    // -------------------------------------------------------------------------
    // 步骤 2：接收侧 —— 先解长度头，再切包体，再反序列化
    // -------------------------------------------------------------------------
    const std::uint32_t bodyLen = im::peekLength(txFrame.data());
    std::printf("[RX] length field = %u, real body size = %zu\n",
                bodyLen, payload.size());

    if (bodyLen != payload.size()) {
        std::printf("  !! FAIL: length field mismatch\n");
        ++failed;
    } else {
        std::printf("  OK: 长度头与包体长度一致\n");
    }
    if (bodyLen > im::kMaxBodyBytes) {
        std::printf("  !! FAIL: bodyLen 超过上限\n");
        ++failed;
    }

    const std::string body = txFrame.substr(im::kLengthFieldBytes, bodyLen);
    im::MsgPackage rxPkg;
    if (!rxPkg.ParseFromString(body)) {
        std::printf("  !! FAIL: MsgPackage 反序列化失败\n");
        ++failed;
    } else if (!im::isKnownMsgType(rxPkg.msg_type())) {
        std::printf("  !! FAIL: 未知 msg_type=%u\n", rxPkg.msg_type());
        ++failed;
    } else {
        im::LoginReq rxReq;
        if (!rxReq.ParseFromString(rxPkg.body())) {
            std::printf("  !! FAIL: LoginReq 反序列化失败\n");
            ++failed;
        } else {
            std::printf("  OK: msg_type=%s username=%s password=%s\n",
                        im::msgTypeName(static_cast<im::MsgType>(rxPkg.msg_type())),
                        rxReq.username().c_str(), rxReq.password().c_str());
        }
    }

    // -------------------------------------------------------------------------
    // 步骤 3：粘包演示 —— 两条帧直接拼在一起，用长度头切回来
    //          ★ Day2 的 class Buffer 就是把下面这个 while 循环封装成成员函数
    // -------------------------------------------------------------------------
    std::string stream;
    stream += txFrame;                     // 第 1 条：登录请求
    stream += buildChatFrame(1, 2, "hello from Day1");  // 第 2 条：私聊

    std::size_t offset = 0;
    int idx = 0;
    while (stream.size() - offset >= im::kLengthFieldBytes) {
        const std::uint32_t len = im::peekLength(stream.data() + offset);

        // 半包：长度头说要 len 字节，但库里还不够 -> 保留，等下次 recv 补上
        if (len > im::kMaxBodyBytes) {
            std::printf("  !! FAIL: 非法长度 %u，应直接断开连接\n", len);
            ++failed;
            break;
        }
        if (stream.size() - offset - im::kLengthFieldBytes < len) {
            std::printf("[SPLIT] 数据不足，剩余 %zu 字节留待下次\n",
                        stream.size() - offset);
            break;
        }

        const std::string one = stream.substr(offset + im::kLengthFieldBytes, len);
        offset += im::kLengthFieldBytes + len;

        im::MsgPackage p;
        if (!p.ParseFromString(one)) {
            std::printf("  !! FAIL: 第 %d 条解析失败\n", idx + 1);
            ++failed;
            continue;
        }
        ++idx;
        std::printf("[SPLIT] #%d msg_type=%s body=%zu bytes\n",
                    idx, im::msgTypeName(static_cast<im::MsgType>(p.msg_type())),
                    p.body().size());
    }

    if (idx == 2 && offset == stream.size()) {
        std::printf("OK: 成功从粘包字节流中切出 2 条完整消息\n");
    } else {
        std::printf("  !! FAIL: 期望切出 2 条，实际 %d 条\n", idx);
        ++failed;
    }

    google::protobuf::ShutdownProtobufLibrary();

    std::printf("\n==== Day1 smoke test %s ====\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
