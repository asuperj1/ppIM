// =============================================================================
//  im_server/src/main.cpp
//  pp（网页版 IM）服务端入口。
//
//  启动后同时监听两条链路：
//      http://127.0.0.1:8080   网页 + WebSocket(/ws)
//      tcp://127.0.0.1:9000    原始 TCP + protobuf（控制台客户端）
//
//  用法：
//      im_server.exe [--http-port 8080] [--tcp-port 9000]
//                    [--domain pp.xiaojietong.online]
//                    [--web-root <web目录>] [--db <sqlite文件>]
// =============================================================================
#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <asio.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "core/chat_service.hpp"
#include "core/database.hpp"
#include "net/tcp_server.hpp"
#include "web/http_server.hpp"

namespace {

struct Options {
    unsigned short http_port{8080};
    unsigned short tcp_port{9000};
    std::string domain{"pp.xiaojietong.online"};  // 仅用于启动横幅 / 文档展示
    std::string web_root;
    std::string db_path;
};

std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len > 0) {
        return std::filesystem::path(buffer, buffer + len).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

void printUsage() {
    std::cout << "用法: im_server [--http-port 8080] [--tcp-port 9000]"
                 " [--domain pp.xiaojietong.online]"
                 " [--web-root <目录>] [--db <文件>]\n";
}

bool parseArgs(int argc, char** argv, Options* opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string* out) -> bool {
            if (i + 1 >= argc) {
                return false;
            }
            *out = argv[++i];
            return true;
        };

        std::string value;
        if (arg == "--http-port") {
            if (!next(&value)) return false;
            opt->http_port = static_cast<unsigned short>(std::atoi(value.c_str()));
        } else if (arg == "--tcp-port") {
            if (!next(&value)) return false;
            opt->tcp_port = static_cast<unsigned short>(std::atoi(value.c_str()));
        } else if (arg == "--domain") {
            if (!next(&value)) return false;
            opt->domain = value;
        } else if (arg == "--web-root") {
            if (!next(&value)) return false;
            opt->web_root = value;
        } else if (arg == "--db") {
            if (!next(&value)) return false;
            opt->db_path = value;
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            std::exit(0);
        } else {
            std::cerr << "未知参数: " << arg << "\n";
            return false;
        }
    }
    return true;
}

std::string resolveWebRoot(const std::string& configured) {
    namespace fs = std::filesystem;

    if (!configured.empty()) {
        return configured;
    }

    const fs::path exe_dir = executableDir();
    const fs::path candidates[] = {
        exe_dir / "web",                 // CMake POST_BUILD 复制到这里
        fs::current_path() / "web",      // 在工程根目录直接运行
        exe_dir / ".." / ".." / "web",   // build/bin/RelWithDebInfo -> 工程根
        exe_dir / ".." / ".." / ".." / "web",
    };

    std::error_code ec;
    for (const auto& dir : candidates) {
        if (fs::is_directory(dir, ec)) {
            return fs::weakly_canonical(dir, ec).string();
        }
    }

    // 一个都不存在也返回默认值，启动后访问会看到 404
    return (exe_dir / "web").string();
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    ::SetConsoleOutputCP(CP_UTF8);
#endif

    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::debug);

    Options opt;
    if (!parseArgs(argc, argv, &opt)) {
        printUsage();
        return 2;
    }

    const std::filesystem::path exe_dir = executableDir();
    if (opt.db_path.empty()) {
        opt.db_path = (exe_dir / "data" / "im.db").string();
    }
    opt.web_root = resolveWebRoot(opt.web_root);

    im::server::Database db;
    if (!db.open(opt.db_path)) {
        spdlog::critical("数据库打开失败: {}", opt.db_path);
        return 1;
    }

    auto chat = std::make_shared<im::server::ChatService>(db, im::server::CosConfig::fromEnv());

    // OAuth 接入方注册（幂等）：每次启动都按环境变量刷新，改回调白名单无需改库。
    //   PP_OAUTH_CLIENT_ID      = xiaojietong-web
    //   PP_OAUTH_CLIENT_SECRET  = <与服务端一致的共享密钥>
    //   PP_OAUTH_CLIENT_NAME    = 校捷通
    //   PP_OAUTH_REDIRECT_URIS  = https://xiaojietong.online/api/v1/auth/pp/callback,http://127.0.0.1:8080/api/v1/auth/pp/callback
    {
        auto envOr = [](const char* key) -> std::string {
#ifdef _WIN32
            char* value = nullptr;
            std::size_t len = 0;
            if (_dupenv_s(&value, &len, key) == 0 && value != nullptr) {
                std::string out(value);
                std::free(value);
                return out;
            }
            return {};
#else
            const char* value = std::getenv(key);
            return value == nullptr ? std::string{} : std::string(value);
#endif
        };

        const std::string client_id = envOr("PP_OAUTH_CLIENT_ID");
        const std::string client_secret = envOr("PP_OAUTH_CLIENT_SECRET");
        if (!client_id.empty() && !client_secret.empty()) {
            const std::string name = envOr("PP_OAUTH_CLIENT_NAME");
            const std::string redirects = envOr("PP_OAUTH_REDIRECT_URIS");
            if (chat->upsertOauthClient(client_id, client_secret, name, redirects)) {
                spdlog::info("[oauth] 接入方已登记: {} ({} 个回调地址)", client_id,
                             redirects.empty() ? 0 : std::count(redirects.begin(), redirects.end(), ',') + 1);
            } else {
                spdlog::warn("[oauth] 接入方登记失败: {}", client_id);
            }
        } else {
            spdlog::info("[oauth] 未配置 OAuth 接入方（PP_OAUTH_CLIENT_ID 为空），跨站登录不可用");
        }
    }

    asio::io_context io;

    im::server::HttpServer http(io,
                                asio::ip::tcp::endpoint(asio::ip::tcp::v4(), opt.http_port),
                                chat,
                                opt.web_root);
    im::server::TcpServer tcp(io,
                              asio::ip::tcp::endpoint(asio::ip::tcp::v4(), opt.tcp_port),
                              chat);

    std::string err;
    if (!http.start(&err)) {
        spdlog::critical("HTTP 服务启动失败: {}", err);
        return 1;
    }
    if (!tcp.start(&err)) {
        spdlog::critical("TCP 服务启动失败: {}", err);
        return 1;
    }

    spdlog::info("==========================================================");
    spdlog::info("  pp 服务端已启动");
    spdlog::info("  网页域名 : http://{}", opt.domain);
    spdlog::info("  本机入口 : http://127.0.0.1:{}/", opt.http_port);
    spdlog::info("  WebSocket: ws://127.0.0.1:{}/ws", opt.http_port);
    spdlog::info("  原始 TCP : 127.0.0.1:{}", opt.tcp_port);
    spdlog::info("  静态目录 : {}", opt.web_root);
    spdlog::info("  数据库   : {}", opt.db_path);
    const auto cos = im::server::CosConfig::fromEnv();
    spdlog::info("  COS 上传 : {}", cos.enabled() ? ("已启用 (" + cos.bucket + ")") : "未配置");
    spdlog::info("==========================================================");

    asio::signal_set signals(io, SIGINT);
    signals.async_wait([&io](const asio::error_code&, int) {
        spdlog::info("收到退出信号，服务端停止");
        io.stop();
    });

    io.run();
    return 0;
}
