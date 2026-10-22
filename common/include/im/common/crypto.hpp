// =============================================================================
//  common/include/im/common/crypto.hpp
//  极简密码学工具：SHA-1 / SHA-256 / Base64 / 随机盐
//
//  为什么自己写而不引 OpenSSL？
//    1) WebSocket 握手只需要一次 SHA-1，引一个几 MB 的库不划算；
//    2) 手写实现本身就是很好的学习材料（消息填充、大端长度、分组迭代）；
//    3) 密码存储只需要 SHA-256 + 随机盐，够用且不引入依赖。
//
//  ⚠️ 生产环境请改用 Argon2/bcrypt 这类抗暴力破解的算法，SHA-256 只适合教学项目。
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace im::crypto {

/// SHA-1 摘要（20 字节）
std::array<std::uint8_t, 20> sha1(std::string_view data);

/// SHA-1 原始二进制串（WebSocket 握手用）
std::string sha1Raw(std::string_view data);

/// SHA-1 十六进制小写字符串（COS 签名用）
std::string sha1Hex(std::string_view data);

/// HMAC-SHA1，返回十六进制小写字符串
std::string hmacSha1Hex(std::string_view key, std::string_view data);

/// SHA-256 摘要（32 字节）
std::array<std::uint8_t, 32> sha256(std::string_view data);

/// SHA-256 十六进制小写字符串（密码存储用）
std::string sha256Hex(std::string_view data);

/// 标准 Base64 编码（带 '=' 填充）
std::string base64(std::string_view data);

/// 生成 n 字节随机数的十六进制串（长度 2n）
std::string randomHex(std::size_t bytes);

}  // namespace im::crypto
