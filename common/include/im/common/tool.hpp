// =============================================================================
//  common/include/im/common/tool.hpp
//  通用工具：网络字节序（大端）长度头编解码 + 十六进制打印
//
//  ★ 为什么需要"长度头"？
//    TCP 是【字节流】而不是【消息流】，它没有消息边界。你 send 两次，
//    对端一次 recv 可能全收到(粘包)，也可能只收到半个(拆包)。
//    所以必须自己定协议划边界，本项目采用最经典的：
//
//        ┌──────────────────┬────────────────────────┐
//        │ 4 字节长度(大端)  │      N 字节包体          │
//        └──────────────────┴────────────────────────┘
//
//    长度字段的值 = 包体字节数 N（不含长度字段自身）。
//    对端流程：先凑够 4 字节 -> 解出 N -> 再凑够 N 字节 -> 才是一条完整消息。
//
//  ★ 为什么用"大端"？为什么不用 htonl？
//    只要收发双方约定一致，大端小端都行；大端是网络协议的传统（"网络字节序"）。
//    这里用位移手写而不是 htonl，理由有两个：
//      (1) htonl 在 Windows 上要拉 winsock2.h，且和 asio 的包含顺序有坑；
//      (2) 手写位移与机器字节序无关，代码本身就证明了"我懂字节序"，
//          面试被问到可以直接指着这 4 行讲。
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace im {

/// 长度字段固定占 4 字节
inline constexpr std::size_t kLengthFieldBytes = 4;

/// 单条消息包体上限 1 MiB。
/// 防御性设计：如果不做上限，对端发一个"长度 = 0xFFFFFFFF"的假包头，
/// 服务端就会傻乎乎地去申请 4GB 缓冲，直接被 DoS 打爆。
inline constexpr std::uint32_t kMaxBodyBytes = 1u * 1024u * 1024u;

/// 主机字节序 -> 大端，写入 4 字节缓冲
inline void writeU32BE(std::uint8_t* dst, std::uint32_t value) noexcept {
    dst[0] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    dst[2] = static_cast<std::uint8_t>((value >>  8) & 0xFFu);
    dst[3] = static_cast<std::uint8_t>( value        & 0xFFu);
}

/// 从 4 字节缓冲读出大端整数
inline std::uint32_t readU32BE(const std::uint8_t* src) noexcept {
    return (static_cast<std::uint32_t>(src[0]) << 24)
         | (static_cast<std::uint32_t>(src[1]) << 16)
         | (static_cast<std::uint32_t>(src[2]) <<  8)
         |  static_cast<std::uint32_t>(src[3]);
}

/// 把包体打包成「4 字节长度头 + 包体」的完整帧，可直接扔给 socket send
inline std::string frame(std::string_view body) {
    std::string out;
    out.resize(kLengthFieldBytes + body.size());
    writeU32BE(reinterpret_cast<std::uint8_t*>(out.data()),
               static_cast<std::uint32_t>(body.size()));
    if (!body.empty()) {
        std::memcpy(out.data() + kLengthFieldBytes, body.data(), body.size());
    }
    return out;
}

/// 读某个字节流位置上的长度头（不校验是否越界，调用方保证至少 4 字节）
inline std::uint32_t peekLength(const char* p) noexcept {
    return readU32BE(reinterpret_cast<const std::uint8_t*>(p));
}

/// 十六进制字符串，抓包/调试用，例："08 01 12 06 61 6C 69 63 65 "
inline std::string toHex(std::string_view data) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 3);
    for (unsigned char c : data) {
        out.push_back(kHex[(c >> 4) & 0x0Fu]);
        out.push_back(kHex[c & 0x0Fu]);
        out.push_back(' ');
    }
    return out;
}

/// 把 asio 的 error_code / 系统错误码转成可读文本（跨平台，不依赖 strerror_s）
std::string errText(int code);

}  // namespace im
