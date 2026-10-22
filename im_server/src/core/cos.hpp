// =============================================================================
//  im_server/src/core/cos.hpp
//  腾讯云 COS 直传签名（POST/PUT 预签名 URL）。
//
//  设计：文件不经过本服务器。
//    1) 前端把 文件名/大小/类型 发给服务端
//    2) 服务端用 SecretId/SecretKey 生成 10 分钟有效的预签名 PUT URL
//    3) 前端直接 PUT 到 COS，再把 file_url 通过 WebSocket 发给聊天对象
//
//  密钥只从环境变量读取，绝不写进代码或返回给前端：
//    COS_SECRET_ID / COS_SECRET_KEY / COS_BUCKET / COS_REGION / COS_HOST(可选)
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace im::server {

struct CosConfig {
    std::string secret_id;
    std::string secret_key;
    std::string bucket;   // 形如 examplebucket-1250000000
    std::string region;   // 形如 ap-shanghai
    std::string host;     // 默认 <bucket>.cos.<region>.myqcloud.com

    bool enabled() const;
    static CosConfig fromEnv();
};

struct CosUploadTicket {
    std::string upload_url;   // 预签名 PUT 地址
    std::string file_url;     // 上传后的公网访问地址
    std::string key;          // 对象键
};

bool buildCosUploadTicket(const CosConfig& cfg,
                          const std::string& key,
                          std::int64_t now,
                          std::int64_t expires_seconds,
                          CosUploadTicket* out,
                          std::string* err);

}  // namespace im::server
