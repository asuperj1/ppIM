// =============================================================================
//  im_server/src/core/json_codec.hpp
//  protobuf <-> JSON 的桥。
//
//  为什么浏览器不直接收发 protobuf 二进制？
//    - 网页端不想额外引入 protobuf.js（还要再维护一份 .proto 的 JS 产物）；
//    - protobuf 官方的 proto3 JSON 映射已经是标准，直接复用 libprotobuf 的
//      json_util，服务端零额外依赖，前端零额外依赖。
//
//  WS 帧格式（文本帧）：
//    {"type": 3, "body": "{\"toUid\":2,\"content\":\"hi\"}"}
//  其中 body 是内层消息的 proto3 JSON 字符串。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace google::protobuf {
class Message;
}

namespace im::jsonc {

/// 把任意 protobuf 消息打印成单行 JSON（int64 能精确表示时输出数字而非字符串）
std::string toJson(const google::protobuf::Message& msg);

/// 从 JSON 解析到 protobuf 消息；未知字段直接忽略，方便协议向前兼容
bool fromJson(std::string_view json, google::protobuf::Message* msg);

/// 把客户端发来的内层 JSON 转成业务层统一使用的 protobuf 二进制
bool toBinary(std::uint32_t type, std::string_view json, std::string* out);

/// 解析 WS 信封，body 输出为内层 JSON 文本
bool parseEnvelope(std::string_view text, std::uint32_t* type, std::string* body_json);

/// 构造 WS 信封 JSON
std::string makeEnvelope(std::uint32_t type, const google::protobuf::Message& inner);

/// 构造一条错误提示信封（type 固定为 13 = ERROR_PUSH）
std::string makeErrorEnvelope(std::int32_t code, std::string_view message);

}  // namespace im::jsonc
