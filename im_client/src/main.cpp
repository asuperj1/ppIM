// =============================================================================
//  im_client/src/main.cpp
//  Day1 占位入口：验证客户端工程也能链上同一套协议与依赖。
//  Day5 会替换成 net_client 的真实控制台交互。
// =============================================================================
#include <cstdio>
#include <string>

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "im/common/msg_type.hpp"
#include "im/common/tool.hpp"
#include "im_msg.pb.h"

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("im_client 骨架启动（Day1）");

    im::RegisterReq reg;
    reg.set_username("alice");
    reg.set_password("123456");
    reg.set_nickname("爱丽丝");

    im::MsgPackage pkg;
    pkg.set_msg_type(static_cast<std::uint32_t>(im::MsgType::kRegisterReq));
    pkg.set_body(reg.SerializeAsString());

    const std::string framed = im::frame(pkg.SerializeAsString());
    spdlog::info("注册帧 msg_type={} 共 {} 字节",
                 im::msgTypeName(im::MsgType::kRegisterReq), framed.size());

    asio::io_context io;   // 仅验证 asio 头文件与库可用

    google::protobuf::ShutdownProtobufLibrary();
    std::printf("im_client skeleton OK (Day1)\n");
    return 0;
}
