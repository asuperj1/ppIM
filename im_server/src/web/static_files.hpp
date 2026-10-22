// =============================================================================
//  im_server/src/web/static_files.hpp
//  极简静态文件服务：把 web/ 目录下的 HTML/CSS/JS 发给浏览器。
//  真实项目请用 Nginx / CDN，这里只是为了"双击一个 exe 就能用"。
// =============================================================================
#pragma once

#include <filesystem>
#include <string>

namespace im::web {

struct StaticFile {
    int status{404};
    std::string content_type{"text/plain; charset=utf-8"};
    std::string body;
    bool is_head_only{false};
};

/// url_path 形如 "/js/app.js" 或 "/"；root 是 web 目录
StaticFile loadStaticFile(const std::filesystem::path& root, const std::string& url_path);

}  // namespace im::web
