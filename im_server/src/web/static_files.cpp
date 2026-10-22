// =============================================================================
//  im_server/src/web/static_files.cpp
// =============================================================================
#include "web/static_files.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace im::web {
namespace {

std::string urlDecode(std::string_view in) {
    std::string out;
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '%' && i + 2 < in.size()) {
            auto hex = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            const int hi = hex(in[i + 1]);
            const int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string extensionOf(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

const char* mimeOf(const std::string& ext) {
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".map") return "application/json; charset=utf-8";
    return "application/octet-stream";
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

StaticFile makeError(int status, std::string text) {
    StaticFile file;
    file.status = status;
    file.content_type = "text/plain; charset=utf-8";
    file.body = std::move(text);
    return file;
}

}  // namespace

StaticFile loadStaticFile(const std::filesystem::path& root, const std::string& url_path) {
    std::string path = urlDecode(url_path);

    // 去掉查询串
    const std::size_t q = path.find('?');
    if (q != std::string::npos) {
        path.resize(q);
    }

    if (path.empty() || path == "/") {
        path = "/index.html";
    }

    // 目录穿越防护：任何 ".." 一律拒绝
    if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
        return makeError(400, "Bad Request");
    }

    std::error_code ec;
    const std::filesystem::path rootAbs = std::filesystem::weakly_canonical(root, ec);
    const std::filesystem::path fileAbs =
        std::filesystem::weakly_canonical(rootAbs / path.substr(1), ec);

    // 解析后的路径必须仍在 web 根目录之内
    const auto rootStr = rootAbs.generic_string();
    const auto fileStr = fileAbs.generic_string();
    if (fileStr.compare(0, rootStr.size(), rootStr) != 0) {
        return makeError(403, "Forbidden");
    }

    if (!std::filesystem::is_regular_file(fileAbs, ec)) {
        return makeError(404, "Not Found: " + path);
    }

    StaticFile file;
    file.status = 200;
    file.content_type = mimeOf(extensionOf(fileAbs));
    file.body = readFile(fileAbs);
    return file;
}

}  // namespace im::web
