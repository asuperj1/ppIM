// =============================================================================
//  im_server/src/web/http_session.cpp
// =============================================================================
#include "web/http_session.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "core/chat_service.hpp"
#include "core/json_codec.hpp"
#include "im/common/msg_type.hpp"
#include "im/common/tool.hpp"
#include "im_msg.pb.h"
#include "web/static_files.hpp"
#include "web/websocket.hpp"

namespace im::server {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trimCopy(std::string value) {
    auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string buildResponse(int status, std::string_view reason,
                          std::string_view content_type, std::string_view body,
                          bool head_only) {
    std::string rsp;
    rsp.reserve(body.size() + 256);
    rsp += "HTTP/1.1 ";
    rsp += std::to_string(status);
    rsp += ' ';
    rsp.append(reason);
    rsp += "\r\nContent-Type: ";
    rsp.append(content_type);
    rsp += "\r\nContent-Length: ";
    rsp += std::to_string(body.size());
    rsp += "\r\nCache-Control: no-cache, no-store, must-revalidate";
    rsp += "\r\nConnection: close\r\n\r\n";
    if (!head_only) {
        rsp.append(body);
    }
    return rsp;
}

}  // namespace

HttpSession::HttpSession(asio::ip::tcp::socket socket,
                         std::shared_ptr<ChatService> chat,
                         std::string web_root)
    : socket_(std::move(socket)),
      idle_timer_(socket_.get_executor()),
      chat_(std::move(chat)),
      web_root_(std::move(web_root)) {
    asio::error_code ec;
    const auto ep = socket_.remote_endpoint(ec);
    if (!ec) {
        remote = ep.address().to_string() + ":" + std::to_string(ep.port());
    }
}

void HttpSession::start() {
    doReadRequest();
    doTick();
}

// ---------------------------------------------------------------------------
//  HTTP
// ---------------------------------------------------------------------------
void HttpSession::doReadRequest() {
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(recv_buf_), [self](const asio::error_code& ec, std::size_t n) {
        if (ec) {
            self->shutdown();
            return;
        }

        self->http_buf_.append(self->recv_buf_.data(), n);

        if (self->http_buf_.size() > 32 * 1024) {
            self->respondAndClose(431, "Request Header Fields Too Large", "text/plain; charset=utf-8",
                                  "请求头过大");
            return;
        }

        const std::size_t end = self->http_buf_.find("\r\n\r\n");
        if (end == std::string::npos) {
            self->doReadRequest();
            return;
        }

        // 解析 Content-Length：OAuth 的 /api/login、/oauth/token 都是 POST JSON
        std::size_t content_length = 0;
        {
            const std::string head = self->http_buf_.substr(0, end);
            const std::size_t pos = toLower(head).find("content-length:");
            if (pos != std::string::npos) {
                std::size_t cursor = pos + std::string("content-length:").size();
                while (cursor < head.size() && std::isspace(static_cast<unsigned char>(head[cursor])) != 0) {
                    ++cursor;
                }
                std::size_t digits = 0;
                while (cursor + digits < head.size() &&
                       std::isdigit(static_cast<unsigned char>(head[cursor + digits])) != 0) {
                    ++digits;
                }
                if (digits > 0) {
                    try {
                        content_length = static_cast<std::size_t>(std::stoull(head.substr(cursor, digits)));
                    } catch (...) {
                        content_length = 0;
                    }
                }
            }
        }

        // 请求体上限 64 KiB —— OAuth 报文里只有短 JSON，不需要更大
        if (content_length > 64 * 1024) {
            self->respondAndClose(413, "Payload Too Large", "text/plain; charset=utf-8",
                                  "请求体过大");
            return;
        }

        const std::size_t body_start = end + 4;
        if (self->http_buf_.size() < body_start + content_length) {
            self->doReadRequest();
            return;
        }

        const std::string head = self->http_buf_.substr(0, end);
        const std::string body = self->http_buf_.substr(body_start, content_length);
        self->handleRequest(head, body);
    });
}

void HttpSession::handleRequest(const std::string& request, const std::string& body) {
    const std::size_t line_end = request.find("\r\n");
    const std::string request_line = request.substr(0, line_end);

    const std::size_t sp1 = request_line.find(' ');
    const std::size_t sp2 = (sp1 == std::string::npos) ? std::string::npos
                                                       : request_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        respondAndClose(400, "Bad Request", "text/plain; charset=utf-8", "Bad Request");
        return;
    }

    const std::string method = request_line.substr(0, sp1);
    std::string path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string query;
    if (const std::size_t q = path.find('?'); q != std::string::npos) {
        query = path.substr(q + 1);
        path = path.substr(0, q);
    }

    // 解析请求头
    std::unordered_map<std::string, std::string> headers;
    std::size_t pos = line_end + 2;
    while (pos < request.size()) {
        const std::size_t eol = request.find("\r\n", pos);
        if (eol == std::string::npos || eol == pos) {
            break;
        }
        const std::string line = request.substr(pos, eol - pos);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            headers[toLower(trimCopy(line.substr(0, colon)))] = trimCopy(line.substr(colon + 1));
        }
        pos = eol + 2;
    }

    // WebSocket 升级
    const bool wants_ws = path == "/ws";
    if (wants_ws) {
        auto upgrade = headers.find("upgrade");
        auto key = headers.find("sec-websocket-key");
        if (upgrade == headers.end() || toLower(upgrade->second) != "websocket" || key == headers.end()) {
            respondAndClose(400, "Bad Request", "text/plain; charset=utf-8",
                            "缺少 WebSocket 握手头");
            return;
        }
        doUpgrade(key->second);
        return;
    }

    // JSON / OAuth 接口
    if (handleApi(method, path, query, headers, body)) {
        return;
    }

    if (method != "GET" && method != "HEAD") {
        respondAndClose(405, "Method Not Allowed", "text/plain; charset=utf-8", "仅支持 GET");
        return;
    }

    // 健康检查，方便部署后探活
    if (path == "/api/health") {
        respondAndClose(200, "OK", "application/json; charset=utf-8",
                        R"({"ok":true,"service":"myim"})");
        return;
    }

    const auto file = im::web::loadStaticFile(web_root_, path);
    if (file.status != 200) {
        const std::string msg = file.body + "\n";
        respondAndClose(file.status,
                        file.status == 404 ? "Not Found" : (file.status == 403 ? "Forbidden" : "Bad Request"),
                        file.content_type, msg, method == "HEAD");
        return;
    }

    respondAndClose(200, "OK", file.content_type, file.body, method == "HEAD");
}

// ---------------------------------------------------------------------------
//  JSON / OAuth 接口
// ---------------------------------------------------------------------------
namespace {

std::string urlDecode(std::string_view in) {
    auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size()) {
            const int hi = hexValue(in[i + 1]);
            const int lo = hexValue(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parseForm(std::string_view q) {
    std::unordered_map<std::string, std::string> out;
    std::size_t pos = 0;
    while (pos <= q.size()) {
        const std::size_t amp = q.find('&', pos);
        const std::string_view pair = q.substr(pos, amp == std::string_view::npos ? std::string_view::npos
                                                                                 : amp - pos);
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            out[urlDecode(pair)] = "";
        }
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return out;
}

std::string bearerOf(const std::unordered_map<std::string, std::string>& headers) {
    auto it = headers.find("authorization");
    if (it == headers.end()) return {};
    const std::string& value = it->second;
    if (value.size() > 7 && toLower(value.substr(0, 7)) == "bearer ") {
        return trimCopy(value.substr(7));
    }
    return {};
}

}  // namespace

bool HttpSession::handleApi(const std::string& method,
                            const std::string& path,
                            const std::string& query,
                            const std::unordered_map<std::string, std::string>& headers,
                            const std::string& body) {
    const auto params = parseForm(query);

    // ------------------------------ 授权确认页 ------------------------------
    if (path == "/oauth/authorize") {
        if (method == "GET" || method == "HEAD") {
            const auto file = im::web::loadStaticFile(web_root_, "/oauth.html");
            respondAndClose(file.status == 200 ? 200 : 500,
                            file.status == 200 ? "OK" : "Internal Server Error",
                            file.content_type, file.body, method == "HEAD");
            return true;
        }

        if (method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        // 用户点了「同意授权」：用 pp 会话令牌确认身份
        UserRow user;
        if (!chat_->resolveSessionToken(bearerOf(headers), &user)) {
            respondAndClose(401, "Unauthorized", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"登录状态已失效，请重新登录"})");
            return true;
        }

        im::OAuthAuthorizeReq req;
        if (!im::jsonc::fromJson(body, &req)) {
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"请求格式错误"})");
            return true;
        }

        im::OAuthAuthorizeRsp rsp;
        std::string err;
        std::string redirect;
        if (!chat_->oauthAuthorize(req.client_id(), req.redirect_uri(), req.state(),
                                   req.scope(), user.uid, &redirect, &err)) {
            rsp.set_success(false);
            rsp.set_err_msg(err);
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        rsp.set_success(true);
        rsp.set_redirect(redirect);
        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // --------------------------- 授权页登录（HTTP）--------------------------
    if (path == "/api/login") {
        if (method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        im::OAuthLoginReq req;
        im::OAuthLoginRsp rsp;
        if (!im::jsonc::fromJson(body, &req)) {
            rsp.set_err_msg("请求格式错误");
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        UserRow user;
        if (!chat_->verifyCredentials(req.username(), req.password(), &user)) {
            rsp.set_err_msg("用户名 / pp 号或密码错误");
            respondAndClose(401, "Unauthorized", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        rsp.set_success(true);
        rsp.set_token(chat_->issueSessionToken(user.uid));
        rsp.set_uid(user.uid);
        rsp.set_nickname(user.nickname);
        rsp.set_pp_id(user.pp_id);
        rsp.set_avatar_url(user.avatar_url);
        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // ------------------------------- 换取令牌 -------------------------------
    if (path == "/oauth/token") {
        if (method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        im::OAuthTokenReq req;
        const auto ct = headers.find("content-type");
        if (ct != headers.end() && toLower(ct->second).find("application/x-www-form-urlencoded") != std::string::npos) {
            const auto form = parseForm(body);
            auto pick = [&form](const char* key) {
                auto it = form.find(key);
                return it == form.end() ? std::string{} : it->second;
            };
            req.set_grant_type(pick("grant_type"));
            req.set_code(pick("code"));
            req.set_client_id(pick("client_id"));
            req.set_client_secret(pick("client_secret"));
            req.set_redirect_uri(pick("redirect_uri"));
        } else if (!im::jsonc::fromJson(body, &req)) {
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"请求格式错误"})");
            return true;
        }

        im::OAuthTokenRsp rsp;
        std::string err;
        if (!chat_->oauthToken(req.grant_type(), req.code(), req.client_id(),
                               req.client_secret(), req.redirect_uri(), &rsp, &err)) {
            rsp.set_success(false);
            rsp.set_err_msg(err);
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // ------------------------------- 撤销授权 -------------------------------
    // POST /oauth/revoke：接入方服务端调用，撤销某个 pp 用户对本应用的授权
    if (path == "/oauth/revoke") {
        if (method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        im::OAuthRevokeReq req;
        const auto ct = headers.find("content-type");
        if (ct != headers.end() && toLower(ct->second).find("application/x-www-form-urlencoded") != std::string::npos) {
            const auto form = parseForm(body);
            auto pick = [&form](const char* key) {
                auto it = form.find(key);
                return it == form.end() ? std::string{} : it->second;
            };
            req.set_client_id(pick("client_id"));
            req.set_client_secret(pick("client_secret"));
            req.set_uid(static_cast<std::uint64_t>(std::strtoull(pick("uid").c_str(), nullptr, 10)));
        } else if (!im::jsonc::fromJson(body, &req)) {
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"请求格式错误"})");
            return true;
        }

        im::OAuthGrantRevokeRsp rsp;
        std::uint32_t revoked = 0;
        std::string err;
        if (!chat_->oauthRevokeForClient(req.client_id(), req.client_secret(), req.uid(), &revoked, &err)) {
            rsp.set_success(false);
            rsp.set_err_msg(err);
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        rsp.set_success(true);
        rsp.set_revoked(revoked);
        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // ------------------------------- 用户信息 -------------------------------
    // GET /api/session：校验 pp 会话令牌（主站与授权页都用它确认“我是谁”）
    if (path == "/api/session") {
        if (method != "GET" && method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        UserRow user;
        if (!chat_->resolveSessionToken(bearerOf(headers), &user)) {
            respondAndClose(401, "Unauthorized", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"登录状态已失效"})");
            return true;
        }

        im::OAuthLoginRsp rsp;
        rsp.set_success(true);
        rsp.set_uid(user.uid);
        rsp.set_nickname(user.nickname);
        rsp.set_pp_id(user.pp_id);
        rsp.set_avatar_url(user.avatar_url);
        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    if (path == "/oauth/userinfo" || path == "/api/userinfo") {
        if (method != "GET" && method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        im::OAuthUserInfoRsp rsp;
        std::string err;
        if (!chat_->oauthUserInfo(bearerOf(headers), &rsp, &err)) {
            rsp.set_success(false);
            rsp.set_err_msg(err);
            respondAndClose(401, "Unauthorized", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // ------------------------------ 授权管理 --------------------------------
    // GET /api/grants：我授权过的应用（Bearer = pp 会话令牌）
    if (path == "/api/grants") {
        if (method != "GET") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        UserRow user;
        if (!chat_->resolveSessionToken(bearerOf(headers), &user)) {
            respondAndClose(401, "Unauthorized", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"登录状态已失效"})");
            return true;
        }

        im::OAuthGrantListRsp rsp;
        chat_->listGrants(user.uid, &rsp);
        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // POST /api/grants/revoke {"clientId":"..."}：解除授权
    if (path == "/api/grants/revoke") {
        if (method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }

        UserRow user;
        if (!chat_->resolveSessionToken(bearerOf(headers), &user)) {
            respondAndClose(401, "Unauthorized", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"登录状态已失效"})");
            return true;
        }

        im::OAuthGrantRevokeReq req;
        im::OAuthGrantRevokeRsp rsp;
        if (!im::jsonc::fromJson(body, &req) || req.client_id().empty()) {
            rsp.set_success(false);
            rsp.set_err_msg("缺少 client_id");
            respondAndClose(400, "Bad Request", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
            return true;
        }

        rsp.set_success(true);
        rsp.set_revoked(chat_->revokeGrant(user.uid, req.client_id()));
        respondAndClose(200, "OK", "application/json; charset=utf-8", im::jsonc::toJson(rsp));
        return true;
    }

    // --------------------------------- 登出 ---------------------------------
    if (path == "/api/logout") {
        if (method != "POST") {
            respondAndClose(405, "Method Not Allowed", "application/json; charset=utf-8",
                            R"({"success":false,"err_msg":"method not allowed"})");
            return true;
        }
        chat_->revokeSessionToken(bearerOf(headers));
        respondAndClose(200, "OK", "application/json; charset=utf-8", R"({"success":true})");
        return true;
    }

    (void)params;
    return false;
}

void HttpSession::doUpgrade(std::string_view client_key) {
    websocket_ = true;
    last_active_ = std::chrono::steady_clock::now();

    std::string rsp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";
    rsp += im::web::WebSocket::acceptKey(client_key);
    rsp += "\r\n\r\n";

    spdlog::debug("[ws] upgrade {}", remote);
    enqueue(std::move(rsp));

    chat_->onOpen(shared_from_this());
    doReadWsHeader();
}

void HttpSession::respondAndClose(int status, std::string_view reason,
                                  std::string_view content_type, std::string_view body,
                                  bool head_only) {
    close_after_write_ = true;
    enqueue(buildResponse(status, reason, content_type, body, head_only));
}

// ---------------------------------------------------------------------------
//  WebSocket 读
// ---------------------------------------------------------------------------
void HttpSession::doReadWsHeader() {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(ws_header_), [self](const asio::error_code& ec, std::size_t) {
        if (ec) {
            self->shutdown();
            return;
        }

        self->last_active_ = std::chrono::steady_clock::now();

        const std::uint8_t b0 = self->ws_header_[0];
        const std::uint8_t b1 = self->ws_header_[1];

        self->ws_fin_ = (b0 & 0x80u) != 0;
        self->ws_opcode_ = static_cast<std::uint8_t>(b0 & 0x0Fu);
        const bool masked = (b1 & 0x80u) != 0;
        const std::uint64_t len = b1 & 0x7Fu;

        if (len == 126) {
            self->doReadWsExtendedLength(2, masked);
        } else if (len == 127) {
            self->doReadWsExtendedLength(8, masked);
        } else {
            self->doReadWsMask(masked, len);
        }
    });
}

void HttpSession::doReadWsExtendedLength(std::size_t bytes, bool masked) {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(ws_ext_.data(), bytes),
                     [self, bytes, masked](const asio::error_code& ec, std::size_t) {
                         if (ec) {
                             self->shutdown();
                             return;
                         }
                         std::uint64_t len = 0;
                         for (std::size_t i = 0; i < bytes; ++i) {
                             len = (len << 8) | self->ws_ext_[i];
                         }
                         self->doReadWsMask(masked, len);
                     });
}

void HttpSession::doReadWsMask(bool masked, std::uint64_t length) {
    if (length > im::kMaxBodyBytes) {
        spdlog::warn("[ws] frame too large: {} bytes from {}", length, remote);
        shutdown();
        return;
    }

    if (!masked) {
        doReadWsPayload(false, length);
        return;
    }

    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(ws_mask_), [self, length](const asio::error_code& ec, std::size_t) {
        if (ec) {
            self->shutdown();
            return;
        }
        self->doReadWsPayload(true, length);
    });
}

void HttpSession::doReadWsPayload(bool masked, std::uint64_t length) {
    if (length == 0) {
        handleWsFrame(ws_opcode_, std::string{}, ws_fin_);
        doReadWsHeader();
        return;
    }

    ws_payload_.assign(static_cast<std::size_t>(length), '\0');

    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(ws_payload_), [self, masked](const asio::error_code& ec, std::size_t) {
        if (ec) {
            self->shutdown();
            return;
        }

        if (masked) {
            for (std::size_t i = 0; i < self->ws_payload_.size(); ++i) {
                self->ws_payload_[i] = static_cast<char>(
                    static_cast<std::uint8_t>(self->ws_payload_[i]) ^ self->ws_mask_[i % 4]);
            }
        }

        std::string payload = std::move(self->ws_payload_);
        self->handleWsFrame(self->ws_opcode_, std::move(payload), self->ws_fin_);
        self->doReadWsHeader();
    });
}

void HttpSession::handleWsFrame(std::uint8_t opcode, std::string payload, bool fin) {
    using im::web::WebSocket;

    switch (opcode) {
        case WebSocket::kContinuation: {
            if (!ws_fragment_active_) {
                shutdown();
                return;
            }
            ws_fragment_ += payload;
            if (ws_fragment_.size() > im::kMaxBodyBytes) {
                shutdown();
                return;
            }
            if (fin) {
                std::string full = std::move(ws_fragment_);
                ws_fragment_.clear();
                ws_fragment_active_ = false;
                if (ws_fragment_opcode_ == WebSocket::kText) {
                    dispatchText(full);
                } else {
                    dispatchBinary(full);
                }
            }
            break;
        }

        case WebSocket::kText:
        case WebSocket::kBinary:
            if (fin) {
                if (opcode == WebSocket::kText) {
                    dispatchText(payload);
                } else {
                    dispatchBinary(payload);
                }
            } else {
                ws_fragment_active_ = true;
                ws_fragment_opcode_ = opcode;
                ws_fragment_ = std::move(payload);
            }
            break;

        case WebSocket::kClose:
            close_after_write_ = true;
            enqueue(WebSocket::encodeClose());
            break;

        case WebSocket::kPing:
            enqueue(WebSocket::encodePong(payload));
            break;

        case WebSocket::kPong:
            break;

        default:
            spdlog::warn("[ws] unsupported opcode {} from {}", opcode, remote);
            break;
    }
}

void HttpSession::dispatchText(const std::string& text) {
    std::uint32_t type = 0;
    std::string body_json;
    if (!im::jsonc::parseEnvelope(text, &type, &body_json)) {
        enqueue(im::jsonc::makeErrorEnvelope(400, "JSON 解析失败"));
        return;
    }

    // 业务层统一吃 protobuf 二进制；WebSocket 只是把 JSON 翻译成二进制再交给它
    std::string body;
    if (!im::jsonc::toBinary(type, body_json, &body)) {
        enqueue(im::jsonc::makeErrorEnvelope(400, "消息字段不合法"));
        return;
    }

    chat_->onPacket(shared_from_this(), type, body);
}

void HttpSession::dispatchBinary(const std::string& data) {
    im::MsgPackage pkg;
    if (!pkg.ParseFromString(data)) {
        enqueue(im::jsonc::makeErrorEnvelope(400, "protobuf 解析失败"));
        return;
    }
    chat_->onPacket(shared_from_this(), pkg.msg_type(), pkg.body());
}

// ---------------------------------------------------------------------------
//  发送
// ---------------------------------------------------------------------------
void HttpSession::sendProto(im::MsgType type, const google::protobuf::Message& msg) {
    if (!websocket_ || closed_) {
        return;
    }
    const std::string json = im::jsonc::makeEnvelope(static_cast<std::uint32_t>(type), msg);
    enqueue(im::web::WebSocket::encodeText(json));
}

void HttpSession::enqueue(std::string data) {
    if (closed_) {
        return;
    }
    const bool idle = out_queue_.empty() && !writing_;
    out_queue_.push_back(std::move(data));
    if (idle) {
        doWrite();
    }
}

void HttpSession::doWrite() {
    if (writing_) {
        return;
    }
    if (out_queue_.empty()) {
        if (close_after_write_) {
            shutdown();
        }
        return;
    }

    writing_ = true;
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(out_queue_.front()),
                      [self](const asio::error_code& ec, std::size_t) {
                          self->writing_ = false;
                          if (ec) {
                              self->shutdown();
                              return;
                          }
                          self->out_queue_.pop_front();
                          self->doWrite();
                      });
}

// ---------------------------------------------------------------------------
//  生命周期
// ---------------------------------------------------------------------------
void HttpSession::doTick() {
    auto self = shared_from_this();
    idle_timer_.expires_after(std::chrono::seconds(20));
    idle_timer_.async_wait([self](const asio::error_code& ec) {
        if (ec || self->closed_) {
            return;
        }

        // 只对 WebSocket 做空闲检测：HTTP 请求本来就该短连接
        if (self->websocket_) {
            const auto idle = std::chrono::steady_clock::now() - self->last_active_;
            if (idle > std::chrono::seconds(90)) {
                spdlog::info("[ws] idle timeout: {}", self->remote);
                self->shutdown();
                return;
            }
        }
        self->doTick();
    });
}

void HttpSession::shutdown() {
    if (closed_) {
        return;
    }
    closed_ = true;

    asio::error_code ec;
    idle_timer_.cancel(ec);
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    if (websocket_ && !notified_close_) {
        notified_close_ = true;
        chat_->onClose(shared_from_this());
    }
}

void HttpSession::close() {
    shutdown();
}

}  // namespace im::server
