// =============================================================================
//  im_server/src/core/cos.cpp
// =============================================================================
#include "core/cos.hpp"

#include <cstddef>
#include <cstdlib>

#include "im/common/crypto.hpp"

namespace im::server {
namespace {

std::string envOf(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) == 0 && buffer != nullptr) {
        std::string value(buffer);
        std::free(buffer);
        return value;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

std::string stripScheme(std::string host) {
    const std::string http = "https://";
    const std::string plain = "http://";
    if (host.rfind(http, 0) == 0) {
        host.erase(0, http.size());
    } else if (host.rfind(plain, 0) == 0) {
        host.erase(0, plain.size());
    }
    while (!host.empty() && host.back() == '/') {
        host.pop_back();
    }
    return host;
}

}  // namespace

bool CosConfig::enabled() const {
    return !secret_id.empty() && !secret_key.empty() && !bucket.empty() &&
           !region.empty() && !host.empty();
}

CosConfig CosConfig::fromEnv() {
    CosConfig cfg;
    cfg.secret_id = envOf("COS_SECRET_ID");
    cfg.secret_key = envOf("COS_SECRET_KEY");
    cfg.bucket = envOf("COS_BUCKET");
    cfg.region = envOf("COS_REGION");
    cfg.host = stripScheme(envOf("COS_HOST"));

    if (cfg.host.empty() && !cfg.bucket.empty() && !cfg.region.empty()) {
        cfg.host = cfg.bucket + ".cos." + cfg.region + ".myqcloud.com";
    }
    return cfg;
}

bool buildCosUploadTicket(const CosConfig& cfg,
                          const std::string& key,
                          std::int64_t now,
                          std::int64_t expires_seconds,
                          CosUploadTicket* out,
                          std::string* err) {
    if (out == nullptr) {
        return false;
    }
    if (!cfg.enabled()) {
        if (err) *err = "COS 未配置";
        return false;
    }
    if (key.empty() || key.front() == '/' || key.find("..") != std::string::npos) {
        if (err) *err = "对象键不合法";
        return false;
    }

    // COS 签名算法（PUT）：
    //   KeyTime   = now;expires
    //   SignKey   = HMAC-SHA1(SecretKey, KeyTime)
    //   HttpString= put\n/<key>\n\nhost=<host>\n
    //   StringToSign = sha1\nKeyTime\nSHA1(HttpString)\n
    //   Signature = HMAC-SHA1(SignKey, StringToSign)
    const std::string key_time =
        std::to_string(now) + ";" + std::to_string(now + expires_seconds);
    const std::string sign_key = im::crypto::hmacSha1Hex(cfg.secret_key, key_time);
    const std::string http_string = "put\n/" + key + "\n\nhost=" + cfg.host + "\n";
    const std::string string_to_sign =
        "sha1\n" + key_time + "\n" + im::crypto::sha1Hex(http_string) + "\n";
    const std::string signature = im::crypto::hmacSha1Hex(sign_key, string_to_sign);

    const std::string base = "https://" + cfg.host + "/" + key;

    out->upload_url = base +
        "?q-sign-algorithm=sha1" +
        "&q-ak=" + cfg.secret_id +
        "&q-sign-time=" + key_time +
        "&q-key-time=" + key_time +
        "&q-header-list=host" +
        "&q-url-param-list=" +
        "&q-signature=" + signature;
    out->file_url = base;
    out->key = key;
    return true;
}

}  // namespace im::server
