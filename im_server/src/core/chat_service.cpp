// =============================================================================
//  im_server/src/core/chat_service.cpp
// =============================================================================
#include "core/chat_service.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <set>
#include <string>

#include <spdlog/spdlog.h>

#include "im/common/crypto.hpp"
#include "im/common/msg_type.hpp"
#include "im_msg.pb.h"

namespace im::server {
namespace {

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// auth_tokens.kind
constexpr std::uint32_t kTokenKindSession = 1;   // pp 自身登录会话
constexpr std::uint32_t kTokenKindCode = 2;      // OAuth 授权码（一次性，60s）
constexpr std::uint32_t kTokenKindAccess = 3;    // OAuth access_token（1h）
constexpr std::int64_t kCodeTtlSeconds = 60;
constexpr std::int64_t kAccessTtlSeconds = 3600;

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool validUsername(const std::string& name) {
    if (name.size() < 3 || name.size() > 20) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    });
}

bool validPassword(const std::string& pwd) {
    return pwd.size() >= 6 && pwd.size() <= 64;
}

bool validNickname(const std::string& nickname) {
    return !nickname.empty() && nickname.size() <= 60;
}

bool validSignature(const std::string& signature) {
    return signature.size() <= 120;
}

bool validPpId(const std::string& pp_id) {
    if (pp_id.size() < 4 || pp_id.size() > 20) return false;
    return std::all_of(pp_id.begin(), pp_id.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    });
}

bool validGroupName(const std::string& name) {
    return !name.empty() && name.size() <= 90;  // 30 个中文
}

constexpr std::uint64_t kMaxFileBytes = 5ull * 1024ull * 1024ull;

bool validFileExt(const std::string& ext) {
    static const std::set<std::string> allowed = {
        "png", "jpg", "jpeg", "gif", "webp",
        // 语音消息（浏览器 MediaRecorder 产物）
        "webm", "ogg", "m4a", "mp3", "wav", "amr", "aac",
        "pdf", "txt", "md", "zip",
        "doc", "docx", "xls", "xlsx", "ppt", "pptx",
    };
    return allowed.count(ext) > 0;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string fileExtension(const std::string& filename) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return {};
    return lowerAscii(filename.substr(dot + 1));
}

std::string baseName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string defaultContentType(const std::string& ext) {
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    // 语音消息：Chrome/Edge 录成 webm(opus)，Safari 录成 m4a(aac)
    if (ext == "webm") return "audio/webm";
    if (ext == "ogg") return "audio/ogg";
    if (ext == "m4a") return "audio/mp4";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "wav") return "audio/wav";
    if (ext == "amr") return "audio/amr";
    if (ext == "aac") return "audio/aac";
    if (ext == "pdf") return "application/pdf";
    if (ext == "txt" || ext == "md") return "text/plain; charset=utf-8";
    if (ext == "zip") return "application/zip";
    return "application/octet-stream";
}

std::string todayKey() {
    const std::time_t t = static_cast<std::time_t>(nowSeconds());
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buffer[16]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d", &tmv);
    return buffer;
}

template <typename T>
bool parseBody(const std::string& body, T* msg) {
    return msg->ParseFromString(body);
}

im::UserInfo makeUserInfo(const UserRow& user, bool online, bool is_friend) {
    im::UserInfo info;
    info.set_uid(user.uid);
    info.set_nickname(user.nickname);
    info.set_pp_id(user.pp_id);
    info.set_signature(user.signature);
    info.set_avatar_url(user.avatar_url);
    info.set_online(online);
    info.set_is_friend(is_friend);
    return info;
}

im::GroupInfo makeGroupInfo(const GroupRow& group) {
    im::GroupInfo info;
    info.set_gid(group.gid);
    info.set_name(group.name);
    info.set_owner_uid(group.owner_uid);
    info.set_member_count(group.member_count);
    return info;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += "、";
        out += names[i];
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
//  连接生命周期
// ---------------------------------------------------------------------------
void ChatService::onOpen(const std::shared_ptr<Peer>& peer) {
    spdlog::debug("[svc] peer connected: {}", peer->remote);
}

void ChatService::onClose(const std::shared_ptr<Peer>& peer) {
    const std::uint64_t uid = peer->uid;
    if (uid != 0) {
        auto it = online_.find(uid);
        if (it != online_.end()) {
            const auto current = it->second.lock();
            if (current && current.get() == peer.get()) {
                online_.erase(it);
                spdlog::info("[svc] user {} ({}) offline", uid, peer->nickname);
            }
        }
        peer->uid = 0;
        broadcastOnline();
    }
    spdlog::debug("[svc] peer closed: {}", peer->remote);
}

void ChatService::onPacket(const std::shared_ptr<Peer>& peer,
                           std::uint32_t type,
                           const std::string& body) {
    if (!im::isKnownMsgType(type)) {
        sendError(peer, 400, "未知消息类型");
        return;
    }

    switch (static_cast<im::MsgType>(type)) {
        case im::MsgType::kRegisterReq:            handleRegister(peer, body); break;
        case im::MsgType::kLoginReq:               handleLogin(peer, body); break;
        case im::MsgType::kSessionLoginReq:        handleSessionLogin(peer, body); break;
        case im::MsgType::kProfileUpdateReq:       handleProfileUpdate(peer, body); break;
        case im::MsgType::kFriendSearchReq:        handleFriendSearch(peer, body); break;
        case im::MsgType::kFriendRequestReq:       handleFriendRequest(peer, body); break;
        case im::MsgType::kFriendRequestListReq:   handleFriendRequestList(peer); break;
        case im::MsgType::kFriendRequestHandleReq: handleFriendRequestHandle(peer, body); break;
        case im::MsgType::kChatPrivate:            handleChatPrivate(peer, body); break;
        case im::MsgType::kHistoryReq:             handleHistory(peer, body); break;
        case im::MsgType::kReadReq:                handleRead(peer, body); break;
        case im::MsgType::kTyping:                 handleTyping(peer, body); break;
        case im::MsgType::kHeartbeat:              handleHeartbeat(peer); break;
        case im::MsgType::kGroupCreateReq:         handleGroupCreate(peer, body); break;
        case im::MsgType::kGroupInviteReq:         handleGroupInvite(peer, body); break;
        case im::MsgType::kGroupQuitReq:           handleGroupQuit(peer, body); break;
        case im::MsgType::kGroupMemberListReq:     handleGroupMemberList(peer, body); break;
        case im::MsgType::kChatGroup:              handleChatGroup(peer, body); break;
        case im::MsgType::kGroupHistoryReq:        handleGroupHistory(peer, body); break;
        case im::MsgType::kGroupReadReq:           handleGroupRead(peer, body); break;
        case im::MsgType::kUploadTicketReq:        handleUploadTicket(peer, body); break;
        case im::MsgType::kFavoriteAddReq:         handleFavoriteAdd(peer, body); break;
        case im::MsgType::kFavoriteListReq:        handleFavoriteList(peer); break;
        case im::MsgType::kFavoriteRemoveReq:      handleFavoriteRemove(peer, body); break;
        case im::MsgType::kRecallReq:              handleRecall(peer, body); break;
        case im::MsgType::kPatReq:                 handlePat(peer, body); break;
        case im::MsgType::kPostCreateReq:          handlePostCreate(peer, body); break;
        case im::MsgType::kFeedReq:                handleFeed(peer, body); break;
        case im::MsgType::kPostLikeReq:            handlePostLike(peer, body); break;
        case im::MsgType::kCommentCreateReq:       handleCommentCreate(peer, body); break;
        case im::MsgType::kPostDeleteReq:          handlePostDelete(peer, body); break;
        default:
            sendError(peer, 400, "该消息类型只能由服务端下发");
            break;
    }
}

// ---------------------------------------------------------------------------
//  注册 / 登录
// ---------------------------------------------------------------------------
void ChatService::handleRegister(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::RegisterReq req;
    im::RegisterRsp rsp;

    if (!parseBody(body, &req)) {
        rsp.set_success(false);
        rsp.set_err_msg("请求格式错误");
        peer->sendProto(im::MsgType::kRegisterRsp, rsp);
        return;
    }

    const std::string username = trim(req.username());
    const std::string nickname = trim(req.nickname());

    if (!validUsername(username)) {
        rsp.set_success(false);
        rsp.set_err_msg("用户名需为 3~20 位字母、数字或下划线");
        peer->sendProto(im::MsgType::kRegisterRsp, rsp);
        return;
    }
    if (!validPassword(req.password())) {
        rsp.set_success(false);
        rsp.set_err_msg("密码需为 6~64 位");
        peer->sendProto(im::MsgType::kRegisterRsp, rsp);
        return;
    }
    if (!validNickname(nickname)) {
        rsp.set_success(false);
        rsp.set_err_msg("昵称为 1~20 个字符");
        peer->sendProto(im::MsgType::kRegisterRsp, rsp);
        return;
    }

    const std::string salt = im::crypto::randomHex(16);
    const std::string hash = im::crypto::sha256Hex(salt + req.password());

    std::uint64_t uid = 0;
    std::string err;
    if (!db_.createUser(username, nickname, salt, hash, &uid, &err)) {
        rsp.set_success(false);
        rsp.set_err_msg(err);
        peer->sendProto(im::MsgType::kRegisterRsp, rsp);
        return;
    }

    UserRow created;
    db_.findUserById(uid, &created);

    spdlog::info("[svc] registered uid={} username={} nickname={} pp_id={}",
                 uid, username, nickname, created.pp_id);

    rsp.set_success(true);
    rsp.set_uid(uid);
    rsp.set_pp_id(created.pp_id);
    rsp.set_err_msg("");
    peer->sendProto(im::MsgType::kRegisterRsp, rsp);

    broadcastOnline();
}

void ChatService::handleLogin(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::LoginReq req;
    im::LoginRsp rsp;

    if (!parseBody(body, &req)) {
        rsp.set_success(false);
        rsp.set_err_msg("请求格式错误");
        peer->sendProto(im::MsgType::kLoginRsp, rsp);
        return;
    }

    // 支持用户名或 pp 号登录：先按用户名找，找不到再按 pp 号找
    const std::string account = trim(req.username());
    UserRow user;
    bool found = db_.findUserByName(account, &user);
    if (!found && !account.empty()) {
        found = db_.findUserByPpId(account, &user);
    }

    if (!found) {
        rsp.set_success(false);
        rsp.set_err_msg("用户名 / pp 号或密码错误");
        peer->sendProto(im::MsgType::kLoginRsp, rsp);
        return;
    }

    if (im::crypto::sha256Hex(user.salt + req.password()) != user.password_hash) {
        rsp.set_success(false);
        rsp.set_err_msg("用户名 / pp 号或密码错误");
        peer->sendProto(im::MsgType::kLoginRsp, rsp);
        spdlog::warn("[svc] login failed for {} from {}", account, peer->remote);
        return;
    }

    rsp.set_success(true);
    rsp.set_uid(user.uid);
    rsp.set_nickname(user.nickname);
    rsp.set_pp_id(user.pp_id);
    rsp.set_signature(user.signature);
    rsp.set_avatar_url(user.avatar_url);
    rsp.set_err_msg("");
    // 长期会话令牌：前端存 localStorage，刷新页面后静默恢复登录
    rsp.set_token(issueSessionToken(user.uid));

    attachUser(peer, user);
    peer->sendProto(im::MsgType::kLoginRsp, rsp);

    spdlog::info("[svc] user {} ({}, {}) online from {}", user.uid, user.nickname, user.pp_id, peer->remote);

    afterLogin(user.uid);
}

// 刷新页面后凭 localStorage 里的令牌静默登录（不需要重新输密码）
void ChatService::handleSessionLogin(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::SessionLoginReq req;
    im::LoginRsp rsp;

    if (!parseBody(body, &req)) {
        rsp.set_success(false);
        rsp.set_err_msg("请求格式错误");
        peer->sendProto(im::MsgType::kSessionLoginRsp, rsp);
        return;
    }

    UserRow user;
    if (req.token().empty() || !resolveSessionToken(req.token(), &user)) {
        rsp.set_success(false);
        rsp.set_err_msg("登录状态已失效，请重新登录");
        peer->sendProto(im::MsgType::kSessionLoginRsp, rsp);
        return;
    }

    rsp.set_success(true);
    rsp.set_uid(user.uid);
    rsp.set_nickname(user.nickname);
    rsp.set_pp_id(user.pp_id);
    rsp.set_signature(user.signature);
    rsp.set_avatar_url(user.avatar_url);
    rsp.set_err_msg("");
    rsp.set_token(req.token());

    attachUser(peer, user);
    peer->sendProto(im::MsgType::kSessionLoginRsp, rsp);

    spdlog::info("[svc] session restored uid={} ({})", user.uid, peer->remote);
    afterLogin(user.uid);
}

// ---------------------------------------------------------------------------
//  登录辅助
// ---------------------------------------------------------------------------
bool ChatService::verifyCredentials(const std::string& account_in,
                                    const std::string& password,
                                    UserRow* out) const {
    if (out == nullptr) return false;

    const std::string account = trim(account_in);
    if (account.empty()) return false;

    UserRow user;
    bool found = db_.findUserByName(account, &user);
    if (!found) {
        found = db_.findUserByPpId(account, &user);
    }
    if (!found) return false;
    if (im::crypto::sha256Hex(user.salt + password) != user.password_hash) return false;

    *out = std::move(user);
    return true;
}

std::string ChatService::issueSessionToken(std::uint64_t uid) {
    constexpr std::int64_t kSessionTtlSeconds = 7 * 24 * 3600;
    return db_.createAuthToken(kTokenKindSession, uid, {}, {}, nowSeconds() + kSessionTtlSeconds);
}

bool ChatService::resolveSessionToken(const std::string& token, UserRow* out) const {
    if (out == nullptr || token.empty()) return false;

    AuthTokenRow row;
    if (!db_.findAuthToken(token, kTokenKindSession, &row)) return false;
    return db_.findUserById(row.uid, out);
}

void ChatService::revokeSessionToken(const std::string& token) {
    if (token.empty()) return;
    db_.deleteAuthToken(token);
}

void ChatService::attachUser(const std::shared_ptr<Peer>& peer, const UserRow& user) {
    auto existing = online_.find(user.uid);
    if (existing != online_.end()) {
        if (auto old = existing->second.lock()) {
            if (old.get() != peer.get()) {
                im::ErrorPush err;
                err.set_code(1001);
                err.set_message("账号已在其他窗口登录");
                old->sendProto(im::MsgType::kErrorPush, err);
                old->uid = 0;
                old->close();
                spdlog::info("[svc] kick previous session of uid={}", user.uid);
            }
        }
        online_.erase(existing);
    }

    peer->uid = user.uid;
    peer->username = user.username;
    peer->nickname = user.nickname;
    peer->pp_id = user.pp_id;
    online_[user.uid] = peer;
}

void ChatService::afterLogin(std::uint64_t uid) {
    broadcastOnline();
    pushFriendList(uid);
    pushGroupList(uid);
    pushConversations(uid);
    pushPendingRequests(uid);
}

// ---------------------------------------------------------------------------
//  OAuth 2.0（授权码模式）
// ---------------------------------------------------------------------------
namespace {

std::string urlEncode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// redirect_uris 是逗号分隔的白名单，必须精确匹配（不做前缀/通配）
bool redirectAllowed(const std::string& list, const std::string& want) {
    std::size_t pos = 0;
    while (pos <= list.size()) {
        const std::size_t comma = list.find(',', pos);
        std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        item = trim(std::move(item));
        if (!item.empty() && item == want) return true;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return false;
}

}  // namespace

bool ChatService::upsertOauthClient(const std::string& client_id,
                                    const std::string& client_secret,
                                    const std::string& name,
                                    const std::string& redirect_uris) {
    if (client_id.empty() || client_secret.empty()) return false;

    OauthClientRow row;
    row.client_id = client_id;
    row.client_secret_hash = im::crypto::sha256Hex(client_secret);
    row.name = name.empty() ? client_id : name;
    row.redirect_uris = redirect_uris;
    return db_.upsertOauthClient(row);
}

bool ChatService::oauthAuthorize(const std::string& client_id,
                                 const std::string& redirect_uri,
                                 const std::string& state,
                                 const std::string& scope,
                                 std::uint64_t uid,
                                 std::string* redirect_url,
                                 std::string* err) {
    if (redirect_url == nullptr) return false;

    OauthClientRow client;
    if (!db_.getOauthClient(client_id, &client)) {
        if (err) *err = "未授权的应用";
        return false;
    }
    if (!redirectAllowed(client.redirect_uris, redirect_uri)) {
        if (err) *err = "回调地址不在白名单内";
        return false;
    }

    // 当前只支持 profile（头像 / 昵称 / pp 号）。不做通配，未知 scope 直接当作 profile，
    // 避免「客户端随便写个字符串就在授权页上显示成已授权全部权限」。
    const std::string granted_scope = "profile";
    (void)scope;

    const std::string code =
        db_.createAuthToken(kTokenKindCode, uid, client_id, redirect_uri,
                            nowSeconds() + kCodeTtlSeconds, granted_scope);
    if (code.empty()) {
        if (err) *err = "授权码签发失败";
        return false;
    }

    std::string url = redirect_uri;
    url += (url.find('?') == std::string::npos) ? '?' : '&';
    url += "code=";
    url += code;
    if (!state.empty()) {
        url += "&state=";
        url += urlEncode(state);
    }

    *redirect_url = std::move(url);
    return true;
}

bool ChatService::oauthToken(const std::string& grant_type,
                             const std::string& code,
                             const std::string& client_id,
                             const std::string& client_secret,
                             const std::string& redirect_uri,
                             im::OAuthTokenRsp* rsp,
                             std::string* err) {
    if (rsp == nullptr) return false;

    if (grant_type != "authorization_code") {
        if (err) *err = "不支持的 grant_type";
        return false;
    }

    OauthClientRow client;
    if (!db_.getOauthClient(client_id, &client)) {
        if (err) *err = "未授权的应用";
        return false;
    }
    if (im::crypto::sha256Hex(client_secret) != client.client_secret_hash) {
        if (err) *err = "client_secret 不正确";
        return false;
    }

    // 一次性消费：并发重放只有一个能成功
    AuthTokenRow row;
    if (!db_.consumeAuthToken(code, kTokenKindCode, &row)) {
        if (err) *err = "授权码无效或已过期";
        return false;
    }
    if (row.client_id != client_id) {
        if (err) *err = "授权码不属于该应用";
        return false;
    }
    if (!redirect_uri.empty() && row.redirect_uri != redirect_uri) {
        if (err) *err = "回调地址与授权时不一致";
        return false;
    }

    UserRow user;
    if (!db_.findUserById(row.uid, &user)) {
        if (err) *err = "用户不存在";
        return false;
    }

    const std::string access =
        db_.createAuthToken(kTokenKindAccess, row.uid, client_id, {}, nowSeconds() + kAccessTtlSeconds);
    if (access.empty()) {
        if (err) *err = "令牌签发失败";
        return false;
    }

    rsp->set_success(true);
    rsp->set_access_token(access);
    rsp->set_token_type("Bearer");
    rsp->set_expires_in(kAccessTtlSeconds);
    rsp->set_uid(user.uid);
    rsp->set_pp_id(user.pp_id);
    rsp->set_nickname(user.nickname);
    rsp->set_avatar_url(user.avatar_url);
    return true;
}

bool ChatService::oauthUserInfo(const std::string& access_token,
                                im::OAuthUserInfoRsp* rsp,
                                std::string* err) {
    if (rsp == nullptr) return false;

    AuthTokenRow row;
    if (!db_.findAuthToken(access_token, kTokenKindAccess, &row)) {
        if (err) *err = "访问令牌无效或已过期";
        return false;
    }

    UserRow user;
    if (!db_.findUserById(row.uid, &user)) {
        if (err) *err = "用户不存在";
        return false;
    }

    rsp->set_success(true);
    rsp->set_uid(user.uid);
    rsp->set_pp_id(user.pp_id);
    rsp->set_nickname(user.nickname);
    rsp->set_avatar_url(user.avatar_url);
    rsp->set_signature(user.signature);
    return true;
}

void ChatService::listGrants(std::uint64_t uid, im::OAuthGrantListRsp* rsp) {
    if (rsp == nullptr) return;

    rsp->set_success(true);
    for (const auto& row : db_.grantsOf(uid)) {
        auto* item = rsp->add_grants();
        item->set_client_id(row.client_id);
        item->set_name(row.name);
        item->set_last_authorized_at(row.last_authorized_at);
        item->set_active_tokens(row.active_tokens);
        item->set_scope(row.scope);
    }
}

std::uint32_t ChatService::revokeGrant(std::uint64_t uid, const std::string& client_id) {
    const std::uint32_t removed = db_.revokeGrants(uid, client_id);
    if (removed > 0) {
        spdlog::info("[oauth] uid={} 解除对 {} 的授权（删除 {} 个令牌）", uid, client_id, removed);
    }
    return removed;
}

bool ChatService::oauthRevokeForClient(const std::string& client_id,
                                       const std::string& client_secret,
                                       std::uint64_t uid,
                                       std::uint32_t* revoked,
                                       std::string* err) {
    if (revoked != nullptr) *revoked = 0;

    OauthClientRow client;
    if (!db_.getOauthClient(client_id, &client)) {
        if (err) *err = "未授权的应用";
        return false;
    }
    if (im::crypto::sha256Hex(client_secret) != client.client_secret_hash) {
        if (err) *err = "client_secret 不正确";
        return false;
    }
    if (uid == 0) {
        if (err) *err = "缺少 uid";
        return false;
    }

    const std::uint32_t removed = db_.revokeGrants(uid, client_id);
    if (revoked != nullptr) *revoked = removed;
    spdlog::info("[oauth] 接入方 {} 撤销 uid={} 的授权（删除 {} 个令牌）", client_id, uid, removed);
    return true;
}

// ---------------------------------------------------------------------------
//  资料
// ---------------------------------------------------------------------------
void ChatService::handleProfileUpdate(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "请先登录");
        return;
    }

    im::ProfileUpdateReq req;
    if (!parseBody(body, &req)) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "请求格式错误");
        return;
    }

    UserRow current;
    if (!db_.findUserById(peer->uid, &current)) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "用户不存在");
        return;
    }

    const std::string nickname = trim(req.nickname()).empty() ? current.nickname : trim(req.nickname());
    const std::string signature = trim(req.signature());
    const std::string pp_id = trim(req.pp_id());
    const std::string avatar_url = trim(req.avatar_url());

    if (!validNickname(nickname)) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "昵称为 1~20 个字符");
        return;
    }
    if (!validSignature(signature)) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "签名最多 40 个汉字");
        return;
    }
    if (!pp_id.empty() && !validPpId(pp_id)) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "pp 号需为 4~20 位字母、数字或下划线");
        return;
    }
    if (!avatar_url.empty()) {
        if (avatar_url.rfind("https://", 0) != 0) {
            sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "头像地址无效");
            return;
        }
        if (cos_.enabled() && avatar_url.find(cos_.host) == std::string::npos) {
            sendOp(peer, im::MsgType::kProfileUpdateRsp, false, "头像地址不属于本服务");
            return;
        }
    }

    std::string err;
    if (!db_.updateProfile(peer->uid, nickname, signature, pp_id, &err, avatar_url)) {
        sendOp(peer, im::MsgType::kProfileUpdateRsp, false, err);
        return;
    }

    peer->nickname = nickname;
    if (!pp_id.empty()) {
        peer->pp_id = pp_id;
    }

    sendOp(peer, im::MsgType::kProfileUpdateRsp, true, "资料已更新");
    broadcastOnline();
    pushFriendList(peer->uid);

    // 好友的联系人列表也要刷新（昵称 / pp 号可能变了）
    for (const auto& friend_user : db_.friendsOf(peer->uid)) {
        pushFriendList(friend_user.uid);
    }
}

// ---------------------------------------------------------------------------
//  好友
// ---------------------------------------------------------------------------
void ChatService::handleFriendSearch(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        return;
    }

    im::FriendSearchReq req;
    if (!parseBody(body, &req)) {
        return;
    }

    im::FriendSearchRsp rsp;
    const std::string keyword = trim(req.keyword());
    if (keyword.empty()) {
        peer->sendProto(im::MsgType::kFriendSearchRsp, rsp);
        return;
    }

    for (const auto& user : db_.searchUsers(keyword, peer->uid, 20)) {
        *rsp.add_users() = makeUserInfo(user, findOnline(user.uid) != nullptr,
                                        db_.areFriends(peer->uid, user.uid));
    }
    peer->sendProto(im::MsgType::kFriendSearchRsp, rsp);
}

void ChatService::handleFriendRequest(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "请先登录");
        return;
    }

    im::FriendRequestReq req;
    if (!parseBody(body, &req)) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "请求格式错误");
        return;
    }

    const std::uint64_t to_uid = req.to_uid();
    if (to_uid == 0 || to_uid == peer->uid) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "目标用户无效");
        return;
    }

    UserRow target;
    if (!db_.findUserById(to_uid, &target)) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "用户不存在");
        return;
    }
    if (db_.areFriends(peer->uid, to_uid)) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "你们已经是好友了");
        return;
    }
    if (db_.hasPendingRequest(peer->uid, to_uid)) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "好友申请已发送，请等待对方处理");
        return;
    }

    // 对方已经先申请过我 -> 直接互相成为好友
    std::uint64_t reverse_id = 0;
    if (db_.findPendingRequestBetween(peer->uid, to_uid, &reverse_id)) {
        FriendRequestRow reverse;
        if (db_.getFriendRequest(reverse_id, &reverse) && reverse.status == 0 && reverse.from_uid == to_uid) {
            db_.setFriendRequestStatus(reverse.id, 1);
            db_.addFriendPair(peer->uid, to_uid);
            pushFriendList(peer->uid);
            pushFriendList(to_uid);
            pushPendingRequests(peer->uid);
            pushPendingRequests(to_uid);
            pushFriendRequestTo(to_uid, reverse.id, 1);
            sendOp(peer, im::MsgType::kFriendRequestRsp, true, "已互相添加为好友");
            return;
        }
    }

    const std::string message = trim(req.message()).empty() ? "你好，我想加你为好友" : trim(req.message());
    const std::uint64_t request_id = db_.createFriendRequest(peer->uid, to_uid, message, nowSeconds());
    if (request_id == 0) {
        sendOp(peer, im::MsgType::kFriendRequestRsp, false, "申请发送失败");
        return;
    }

    pushFriendRequestTo(to_uid, request_id, 0);
    sendOp(peer, im::MsgType::kFriendRequestRsp, true, "好友申请已发送");
    spdlog::info("[svc] friend request {} -> {}", peer->uid, to_uid);
}

void ChatService::handleFriendRequestList(const std::shared_ptr<Peer>& peer) {
    if (!peer->isLoggedIn()) {
        return;
    }
    pushPendingRequests(peer->uid);
}

void ChatService::handleFriendRequestHandle(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendOp(peer, im::MsgType::kFriendRequestHandleRsp, false, "请先登录");
        return;
    }

    im::FriendRequestHandleReq req;
    if (!parseBody(body, &req)) {
        sendOp(peer, im::MsgType::kFriendRequestHandleRsp, false, "请求格式错误");
        return;
    }

    FriendRequestRow request;
    if (!db_.getFriendRequest(req.request_id(), &request)) {
        sendOp(peer, im::MsgType::kFriendRequestHandleRsp, false, "申请不存在");
        return;
    }
    if (request.to_uid != peer->uid) {
        sendOp(peer, im::MsgType::kFriendRequestHandleRsp, false, "无权处理该申请");
        return;
    }
    if (request.status != 0) {
        sendOp(peer, im::MsgType::kFriendRequestHandleRsp, false, "该申请已处理");
        return;
    }

    const std::uint32_t status = req.accept() ? 1u : 2u;
    db_.setFriendRequestStatus(request.id, status);

    if (req.accept()) {
        db_.addFriendPair(request.from_uid, request.to_uid);
        pushFriendList(request.from_uid);
        pushFriendList(request.to_uid);
        spdlog::info("[svc] friend accepted: {} <-> {}", request.from_uid, request.to_uid);
    }

    pushFriendRequestTo(request.from_uid, request.id, status);
    pushPendingRequests(peer->uid);
    sendOp(peer, im::MsgType::kFriendRequestHandleRsp, true,
           req.accept() ? "已添加为好友" : "已拒绝该申请");
}

// ---------------------------------------------------------------------------
//  私聊
// ---------------------------------------------------------------------------
void ChatService::handleChatPrivate(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::ChatPrivate req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "消息格式错误");
        return;
    }

    const std::uint64_t to_uid = req.to_uid();
    const std::uint32_t msg_kind = req.msg_kind() == 0 ? 1u : req.msg_kind();
    std::string content = req.content();
    std::string file_url = req.file_url();
    std::string file_name = baseName(req.file_name());
    std::uint64_t file_size = req.file_size();
    // 引用回复：只作展示（原文摘要在 quote_text 里），服务端不做一致性校验
    const std::string quote_id = req.quote_id().substr(0, 64);
    const std::string quote_text = req.quote_text().substr(0, 300);
    const std::string quote_nickname = req.quote_nickname().substr(0, 64);

    if (to_uid == 0 || to_uid == peer->uid) {
        sendError(peer, 400, "收件人无效");
        return;
    }
    if (!db_.areFriends(peer->uid, to_uid)) {
        sendError(peer, 403, "对方还不是你的好友，请先添加好友");
        return;
    }

    if (msg_kind == 1) {
        if (content.empty() || content.size() > 4000) {
            sendError(peer, 400, "消息内容为空或过长");
            return;
        }
        file_url.clear();
        file_name.clear();
        file_size = 0;
    } else if (msg_kind == 2 || msg_kind == 3) {
        if (file_url.rfind("https://", 0) != 0) {
            sendError(peer, 400, "文件地址无效");
            return;
        }
        if (cos_.enabled() && file_url.find(cos_.host) == std::string::npos) {
            sendError(peer, 400, "文件地址不属于本服务");
            return;
        }
        if (file_name.empty() || file_name.size() > 200) {
            sendError(peer, 400, "文件名无效");
            return;
        }
        if (file_size == 0 || file_size > kMaxFileBytes) {
            sendError(peer, 400, "文件大小需在 5MB 以内");
            return;
        }
        content = msg_kind == 2 ? "[图片]" : ("[文件] " + file_name);
    } else {
        sendError(peer, 400, "消息类型无效");
        return;
    }

    UserRow receiver;
    if (!db_.findUserById(to_uid, &receiver)) {
        sendError(peer, 404, "收件人不存在");
        return;
    }

    const std::int64_t ts = nowSeconds();
    const std::uint64_t msg_id = db_.insertMessage(peer->uid, to_uid, content, ts,
                                                   msg_kind, file_url, file_name, file_size,
                                                   quote_id, quote_text, quote_nickname);
    if (msg_id == 0) {
        sendError(peer, 500, "消息保存失败");
        return;
    }

    im::ChatAck ack;
    ack.set_msg_id(msg_id);
    ack.set_client_msg_id(req.client_msg_id());
    ack.set_timestamp(ts);
    ack.set_to_uid(to_uid);
    peer->sendProto(im::MsgType::kChatAck, ack);

    im::ChatPrivate out;
    out.set_from_uid(peer->uid);
    out.set_to_uid(to_uid);
    out.set_content(content);
    out.set_timestamp(ts);
    out.set_msg_id(msg_id);
    out.set_client_msg_id(req.client_msg_id());
    out.set_from_nickname(peer->nickname);
    out.set_msg_kind(msg_kind);
    out.set_file_url(file_url);
    out.set_file_name(file_name);
    out.set_file_size(file_size);
    out.set_quote_id(quote_id);
    out.set_quote_text(quote_text);
    out.set_quote_nickname(quote_nickname);

    if (auto target = findOnline(to_uid)) {
        target->sendProto(im::MsgType::kChatPrivate, out);
    }
}

void ChatService::handleHistory(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::HistoryReq req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "请求格式错误");
        return;
    }

    bool has_more = false;
    const auto rows = db_.history(peer->uid, req.peer_uid(), req.before_id(), req.limit(), &has_more);

    im::HistoryRsp rsp;
    rsp.set_peer_uid(req.peer_uid());
    rsp.set_has_more(has_more);

    for (const auto& row : rows) {
        auto* msg = rsp.add_messages();
        msg->set_from_uid(row.from_uid);
        msg->set_to_uid(row.to_uid);
        msg->set_content(row.content);
        msg->set_timestamp(row.created_at);
        msg->set_msg_id(row.id);
        msg->set_msg_kind(row.msg_kind);
        msg->set_file_url(row.file_url);
        msg->set_file_name(row.file_name);
        msg->set_file_size(row.file_size);
        msg->set_quote_id(row.quote_id);
        msg->set_quote_text(row.quote_text);
        msg->set_quote_nickname(row.quote_nickname);
        msg->set_recalled(row.recalled);
    }
    peer->sendProto(im::MsgType::kHistoryRsp, rsp);
}

void ChatService::handleRead(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) return;
    im::ReadReq req;
    if (!parseBody(body, &req)) return;
    db_.markRead(peer->uid, req.peer_uid());
}

void ChatService::handleTyping(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) return;

    im::Typing req;
    if (!parseBody(body, &req)) return;

    if (auto target = findOnline(req.to_uid())) {
        im::Typing forwarded;
        forwarded.set_to_uid(peer->uid);   // 下发给接收方时填「发送者 uid」
        forwarded.set_typing(req.typing());
        target->sendProto(im::MsgType::kTyping, forwarded);
    }
}

void ChatService::handleHeartbeat(const std::shared_ptr<Peer>& peer) {
    im::HeartBeat beat;
    beat.set_server_time(nowSeconds());
    peer->sendProto(im::MsgType::kHeartbeat, beat);
}

// ---------------------------------------------------------------------------
//  群聊
// ---------------------------------------------------------------------------
void ChatService::handleGroupCreate(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendOp(peer, im::MsgType::kGroupCreateRsp, false, "请先登录");
        return;
    }

    im::GroupCreateReq req;
    if (!parseBody(body, &req)) {
        sendOp(peer, im::MsgType::kGroupCreateRsp, false, "请求格式错误");
        return;
    }

    const std::string name = trim(req.name());
    if (!validGroupName(name)) {
        sendOp(peer, im::MsgType::kGroupCreateRsp, false, "群名称需为 1~30 个字符");
        return;
    }

    std::set<std::uint64_t> picked;
    for (std::uint64_t uid : req.member_uids()) {
        if (uid == peer->uid || uid == 0) continue;
        if (!db_.areFriends(peer->uid, uid)) continue;   // 只允许拉好友
        picked.insert(uid);
    }
    if (picked.size() > 50) {
        sendOp(peer, im::MsgType::kGroupCreateRsp, false, "群成员最多 50 人");
        return;
    }

    const std::vector<std::uint64_t> members(picked.begin(), picked.end());
    const std::int64_t ts = nowSeconds();
    const std::uint64_t gid = db_.createGroup(name, peer->uid, members, ts);
    if (gid == 0) {
        sendOp(peer, im::MsgType::kGroupCreateRsp, false, "创建群聊失败");
        return;
    }

    db_.insertGroupMessage(gid, 0, peer->nickname + " 创建了群聊「" + name + "」", 2, ts);

    GroupRow group;
    db_.getGroup(gid, &group);

    im::GroupCreateRsp rsp;
    rsp.set_success(true);
    *rsp.mutable_group() = makeGroupInfo(group);
    peer->sendProto(im::MsgType::kGroupCreateRsp, rsp);

    pushGroupListsOf(gid);
    sendSystemMessage(gid, peer->nickname + " 邀请 " + std::to_string(picked.size()) + " 位好友加入了群聊", ts);
    spdlog::info("[svc] group {} created by {} ({} members)", gid, peer->uid, group.member_count);
}

void ChatService::handleGroupInvite(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendOp(peer, im::MsgType::kGroupInviteRsp, false, "请先登录");
        return;
    }

    im::GroupInviteReq req;
    if (!parseBody(body, &req)) {
        sendOp(peer, im::MsgType::kGroupInviteRsp, false, "请求格式错误");
        return;
    }

    const std::uint64_t gid = req.gid();
    if (!db_.isGroupMember(gid, peer->uid)) {
        sendOp(peer, im::MsgType::kGroupInviteRsp, false, "你不在该群聊中");
        return;
    }
    if (db_.groupMemberCount(gid) >= 50) {
        sendOp(peer, im::MsgType::kGroupInviteRsp, false, "群成员已满（最多 50 人）");
        return;
    }

    std::vector<std::string> added_names;
    std::uint32_t current_count = db_.groupMemberCount(gid);
    for (std::uint64_t uid : req.member_uids()) {
        if (current_count >= 50) break;
        if (uid == 0 || db_.isGroupMember(gid, uid)) continue;
        if (!db_.areFriends(peer->uid, uid)) continue;
        UserRow user;
        if (!db_.findUserById(uid, &user)) continue;

        db_.addGroupMember(gid, uid, 0, nowSeconds());
        added_names.push_back(user.nickname);
        ++current_count;
    }

    if (added_names.empty()) {
        sendOp(peer, im::MsgType::kGroupInviteRsp, false, "没有可邀请的好友");
        return;
    }

    sendSystemMessage(gid, peer->nickname + " 邀请 " + joinNames(added_names) + " 加入了群聊", nowSeconds());
    pushGroupListsOf(gid);
    sendOp(peer, im::MsgType::kGroupInviteRsp, true, "已邀请 " + std::to_string(added_names.size()) + " 人");
}

void ChatService::handleGroupQuit(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendOp(peer, im::MsgType::kGroupQuitRsp, false, "请先登录");
        return;
    }

    im::GroupQuitReq req;
    if (!parseBody(body, &req)) {
        sendOp(peer, im::MsgType::kGroupQuitRsp, false, "请求格式错误");
        return;
    }

    const std::uint64_t gid = req.gid();
    GroupRow group;
    if (!db_.getGroup(gid, &group)) {
        sendOp(peer, im::MsgType::kGroupQuitRsp, false, "群聊不存在");
        return;
    }
    if (!db_.isGroupMember(gid, peer->uid)) {
        sendOp(peer, im::MsgType::kGroupQuitRsp, false, "你不在该群聊中");
        return;
    }

    const auto members = memberIds(gid);

    if (group.owner_uid == peer->uid) {
        // 群主退出 = 解散
        db_.deleteGroup(gid);
        for (std::uint64_t uid : members) {
            pushGroupList(uid);
            pushConversations(uid);
        }
        sendOp(peer, im::MsgType::kGroupQuitRsp, true, "群聊已解散");
        spdlog::info("[svc] group {} dissolved by owner {}", gid, peer->uid);
        return;
    }

    db_.removeGroupMember(gid, peer->uid);
    sendSystemMessage(gid, peer->nickname + " 退出了群聊", nowSeconds());
    pushGroupListsOf(gid);
    pushGroupList(peer->uid);
    pushConversations(peer->uid);
    sendOp(peer, im::MsgType::kGroupQuitRsp, true, "已退出群聊");
}

void ChatService::handleGroupMemberList(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) return;

    im::GroupMemberListReq req;
    if (!parseBody(body, &req)) return;

    if (!db_.isGroupMember(req.gid(), peer->uid)) {
        sendError(peer, 403, "你不在该群聊中");
        return;
    }

    im::GroupMemberListRsp rsp;
    rsp.set_gid(req.gid());
    for (const auto& member : db_.groupMembers(req.gid())) {
        auto* info = rsp.add_members();
        info->set_uid(member.uid);
        info->set_nickname(member.nickname);
        info->set_pp_id(member.pp_id);
        info->set_role(member.role);
        info->set_online(findOnline(member.uid) != nullptr);
    }
    peer->sendProto(im::MsgType::kGroupMemberListRsp, rsp);
}

void ChatService::handleChatGroup(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::ChatGroup req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "消息格式错误");
        return;
    }

    const std::uint64_t gid = req.gid();
    if (!db_.isGroupMember(gid, peer->uid)) {
        sendError(peer, 403, "你不在该群聊中");
        return;
    }

    const std::uint32_t msg_kind = req.msg_kind() == 0 ? 1u : req.msg_kind();
    std::string content = req.content();
    std::string file_url = req.file_url();
    std::string file_name = baseName(req.file_name());
    std::uint64_t file_size = req.file_size();
    const std::string quote_id = req.quote_id().substr(0, 64);
    const std::string quote_text = req.quote_text().substr(0, 300);
    const std::string quote_nickname = req.quote_nickname().substr(0, 64);

    if (msg_kind == 1) {
        if (content.empty() || content.size() > 4000) {
            sendError(peer, 400, "消息内容为空或过长");
            return;
        }
        file_url.clear();
        file_name.clear();
        file_size = 0;
    } else if (msg_kind == 2 || msg_kind == 3) {
        if (file_url.rfind("https://", 0) != 0) {
            sendError(peer, 400, "文件地址无效");
            return;
        }
        if (cos_.enabled() && file_url.find(cos_.host) == std::string::npos) {
            sendError(peer, 400, "文件地址不属于本服务");
            return;
        }
        if (file_name.empty() || file_name.size() > 200) {
            sendError(peer, 400, "文件名无效");
            return;
        }
        if (file_size == 0 || file_size > kMaxFileBytes) {
            sendError(peer, 400, "文件大小需在 5MB 以内");
            return;
        }
        content = msg_kind == 2 ? "[图片]" : ("[文件] " + file_name);
    } else {
        sendError(peer, 400, "消息类型无效");
        return;
    }

    const std::int64_t ts = nowSeconds();
    const std::uint64_t msg_id = db_.insertGroupMessage(gid, peer->uid, content, 1, ts,
                                                        msg_kind, file_url, file_name, file_size,
                                                        quote_id, quote_text, quote_nickname);
    if (msg_id == 0) {
        sendError(peer, 500, "消息保存失败");
        return;
    }

    im::ChatGroupAck ack;
    ack.set_gid(gid);
    ack.set_msg_id(msg_id);
    ack.set_client_msg_id(req.client_msg_id());
    ack.set_timestamp(ts);
    peer->sendProto(im::MsgType::kChatGroupAck, ack);

    im::ChatGroup out;
    out.set_gid(gid);
    out.set_from_uid(peer->uid);
    out.set_from_nickname(peer->nickname);
    out.set_content(content);
    out.set_timestamp(ts);
    out.set_msg_id(msg_id);
    out.set_client_msg_id(req.client_msg_id());
    out.set_kind(1);
    out.set_msg_kind(msg_kind);
    out.set_file_url(file_url);
    out.set_file_name(file_name);
    out.set_file_size(file_size);
    out.set_quote_id(quote_id);
    out.set_quote_text(quote_text);
    out.set_quote_nickname(quote_nickname);

    for (std::uint64_t uid : memberIds(gid)) {
        if (uid == peer->uid) continue;
        if (auto target = findOnline(uid)) {
            target->sendProto(im::MsgType::kChatGroup, out);
        }
    }
}

void ChatService::handleGroupHistory(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) return;

    im::GroupHistoryReq req;
    if (!parseBody(body, &req)) return;

    if (!db_.isGroupMember(req.gid(), peer->uid)) {
        sendError(peer, 403, "你不在该群聊中");
        return;
    }

    bool has_more = false;
    const auto rows = db_.groupHistory(req.gid(), req.before_id(), req.limit(), &has_more);

    im::GroupHistoryRsp rsp;
    rsp.set_gid(req.gid());
    rsp.set_has_more(has_more);

    for (const auto& row : rows) {
        auto* msg = rsp.add_messages();
        msg->set_gid(row.group_id);
        msg->set_from_uid(row.from_uid);
        msg->set_content(row.content);
        msg->set_timestamp(row.created_at);
        msg->set_msg_id(row.id);
        msg->set_kind(row.kind);
        msg->set_msg_kind(row.msg_kind);
        msg->set_file_url(row.file_url);
        msg->set_file_name(row.file_name);
        msg->set_file_size(row.file_size);
        msg->set_quote_id(row.quote_id);
        msg->set_quote_text(row.quote_text);
        msg->set_quote_nickname(row.quote_nickname);
        msg->set_recalled(row.recalled);
        if (row.from_uid != 0) {
            UserRow user;
            if (db_.findUserById(row.from_uid, &user)) {
                msg->set_from_nickname(user.nickname);
            }
        }
    }
    peer->sendProto(im::MsgType::kGroupHistoryRsp, rsp);
}

void ChatService::handleGroupRead(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) return;

    im::GroupReadReq req;
    if (!parseBody(body, &req)) return;
    if (!db_.isGroupMember(req.gid(), peer->uid)) return;
    db_.markGroupRead(peer->uid, req.gid());
}

// ---------------------------------------------------------------------------
//  收藏夹（长按消息 → 收藏）
// ---------------------------------------------------------------------------
void ChatService::handleFavoriteAdd(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::FavoriteAddReq req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "请求格式错误");
        return;
    }

    im::FavoriteAddRsp rsp;
    const std::string msg_id = req.msg_id().substr(0, 64);
    if (msg_id.empty()) {
        rsp.set_success(false);
        rsp.set_err_msg("消息 ID 无效");
        peer->sendProto(im::MsgType::kFavoriteAddRsp, rsp);
        return;
    }

    FavoriteRow row;
    row.msg_id = msg_id;
    row.msg_kind = req.msg_kind() == 0 ? 1u : req.msg_kind();
    row.content = req.content().substr(0, 4000);
    row.file_url = req.file_url();
    row.file_name = req.file_name().substr(0, 200);
    row.file_size = req.file_size();
    row.from_nickname = req.from_nickname().substr(0, 64);
    row.timestamp = req.timestamp();
    row.created_at = nowSeconds();

    if (!db_.addFavorite(peer->uid, row)) {
        rsp.set_success(false);
        rsp.set_err_msg("收藏失败，请稍后重试");
        peer->sendProto(im::MsgType::kFavoriteAddRsp, rsp);
        return;
    }

    rsp.set_success(true);
    peer->sendProto(im::MsgType::kFavoriteAddRsp, rsp);
    spdlog::info("[svc] favorite add uid={} msg={}", peer->uid, msg_id);
}

void ChatService::handleFavoriteList(const std::shared_ptr<Peer>& peer) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::FavoriteListRsp rsp;
    for (const auto& row : db_.favoritesOf(peer->uid)) {
        auto* item = rsp.add_items();
        item->set_msg_id(row.msg_id);
        item->set_msg_kind(row.msg_kind);
        item->set_content(row.content);
        item->set_file_url(row.file_url);
        item->set_file_name(row.file_name);
        item->set_file_size(row.file_size);
        item->set_from_nickname(row.from_nickname);
        item->set_timestamp(row.timestamp);
        item->set_created_at(row.created_at);
    }
    peer->sendProto(im::MsgType::kFavoriteListRsp, rsp);
}

void ChatService::handleFavoriteRemove(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::FavoriteRemoveReq req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "请求格式错误");
        return;
    }

    im::FavoriteRemoveRsp rsp;
    rsp.set_success(db_.removeFavorite(peer->uid, req.msg_id().substr(0, 64)));
    if (!rsp.success()) rsp.set_err_msg("取消收藏失败");
    peer->sendProto(im::MsgType::kFavoriteRemoveRsp, rsp);
}

// ---------------------------------------------------------------------------
//  撤回（自己发的消息，2 分钟内；与微信一致）
// ---------------------------------------------------------------------------
namespace {
constexpr std::int64_t kRecallWindowSeconds = 120;
}

void ChatService::handleRecall(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::RecallReq req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "请求格式错误");
        return;
    }

    im::RecallRsp rsp;
    rsp.set_msg_id(req.msg_id());

    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kRecallRsp, rsp);
    };

    if (req.msg_id() == 0) {
        fail("消息 ID 无效");
        return;
    }

    MessageRow row;
    if (!db_.getMessage(req.msg_id(), &row)) {
        fail("消息不存在");
        return;
    }

    // 群聊：必须是群成员；私聊：必须是会话双方之一
    if (row.group_id != 0) {
        if (!db_.isGroupMember(row.group_id, peer->uid)) {
            fail("无权操作该消息");
            return;
        }
    } else if (row.from_uid != peer->uid && row.to_uid != peer->uid) {
        fail("无权操作该消息");
        return;
    }

    if (row.from_uid != peer->uid) {
        fail("只能撤回自己发送的消息");
        return;
    }
    if (row.recalled) {
        fail("消息已撤回");
        return;
    }
    if (nowSeconds() - row.created_at > kRecallWindowSeconds) {
        fail("超过 2 分钟的消息不能撤回");
        return;
    }
    if (!db_.recallMessage(row.id, peer->uid)) {
        fail("撤回失败，请稍后重试");
        return;
    }

    rsp.set_success(true);
    peer->sendProto(im::MsgType::kRecallRsp, rsp);

    im::RecallPush push;
    push.set_msg_id(row.id);
    push.set_from_uid(row.from_uid);
    push.set_from_nickname(peer->nickname);
    if (row.group_id != 0) {
        push.set_gid(row.group_id);
        for (const auto& member : db_.groupMembers(row.group_id)) {
            if (member.uid == peer->uid) continue;
            if (auto target = findOnline(member.uid)) {
                target->sendProto(im::MsgType::kRecallPush, push);
            }
        }
    } else {
        push.set_peer_uid(row.to_uid);
        if (auto target = findOnline(row.to_uid)) {
            target->sendProto(im::MsgType::kRecallPush, push);
        }
    }

    spdlog::info("[svc] recall msg={} by uid={} gid={}", row.id, peer->uid, row.group_id);
}

// ---------------------------------------------------------------------------
//  拍一拍（双击头像）
//  存成 msg_kind = 4 的普通消息，因此可被 RecallReq 撤回（无需额外分支）。
// ---------------------------------------------------------------------------
void ChatService::handlePat(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::PatReq req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "请求格式错误");
        return;
    }

    im::PatRsp rsp;
    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kPatRsp, rsp);
    };

    const std::int64_t ts = nowSeconds();

    // ---- 群聊：拍群里的人 ----
    if (req.gid() != 0) {
        const std::uint64_t gid = req.gid();
        if (!db_.isGroupMember(gid, peer->uid)) {
            fail("你不在该群里");
            return;
        }
        const std::uint64_t target_uid = req.target_uid();
        if (target_uid == 0 || !db_.isGroupMember(gid, target_uid)) {
            fail("被拍的人不在群里");
            return;
        }
        UserRow target;
        if (!db_.findUserById(target_uid, &target)) {
            fail("用户不存在");
            return;
        }

        const std::string target_name = target.nickname.substr(0, 64);
        const std::uint64_t msg_id =
            db_.insertGroupMessage(gid, peer->uid, target_name, 1, ts, 4);
        if (msg_id == 0) {
            fail("拍一拍失败，请稍后重试");
            return;
        }

        rsp.set_success(true);
        rsp.set_msg_id(msg_id);
        rsp.set_timestamp(ts);
        rsp.set_target_nickname(target_name);
        rsp.set_gid(gid);
        rsp.set_target_uid(target_uid);
        peer->sendProto(im::MsgType::kPatRsp, rsp);

        im::PatPush push;
        push.set_msg_id(msg_id);
        push.set_from_uid(peer->uid);
        push.set_from_nickname(peer->nickname);
        push.set_gid(gid);
        push.set_target_uid(target_uid);
        push.set_target_nickname(target_name);
        push.set_timestamp(ts);
        for (const auto& member : db_.groupMembers(gid)) {
            if (member.uid == peer->uid) continue;   // 自己已由 rsp 更新
            if (auto conn = findOnline(member.uid)) {
                conn->sendProto(im::MsgType::kPatPush, push);
            }
        }
        spdlog::info("[svc] pat gid={} from={} target={} msg={}", gid, peer->uid, target_uid, msg_id);
        return;
    }

    // ---- 私聊：拍对方 ----
    const std::uint64_t to_uid = req.peer_uid();
    if (to_uid == 0 || to_uid == peer->uid) {
        fail("收件人无效");
        return;
    }
    if (!db_.areFriends(peer->uid, to_uid)) {
        fail("对方还不是你的好友，请先添加好友");
        return;
    }
    UserRow receiver;
    if (!db_.findUserById(to_uid, &receiver)) {
        fail("收件人不存在");
        return;
    }

    const std::string target_name = receiver.nickname.substr(0, 64);
    const std::uint64_t msg_id = db_.insertMessage(peer->uid, to_uid, target_name, ts, 4);
    if (msg_id == 0) {
        fail("拍一拍失败，请稍后重试");
        return;
    }

    rsp.set_success(true);
    rsp.set_msg_id(msg_id);
    rsp.set_timestamp(ts);
    rsp.set_target_nickname(target_name);
    rsp.set_peer_uid(to_uid);
    rsp.set_target_uid(to_uid);
    peer->sendProto(im::MsgType::kPatRsp, rsp);

    im::PatPush push;
    push.set_msg_id(msg_id);
    push.set_from_uid(peer->uid);
    push.set_from_nickname(peer->nickname);
    push.set_peer_uid(to_uid);
    push.set_target_uid(to_uid);
    push.set_target_nickname(target_name);
    push.set_timestamp(ts);
    if (auto target = findOnline(to_uid)) {
        target->sendProto(im::MsgType::kPatPush, push);
    }
    spdlog::info("[svc] pat from={} target={} msg={}", peer->uid, to_uid, msg_id);
}

// ---------------------------------------------------------------------------
//  COS 直传票据
// ---------------------------------------------------------------------------
void ChatService::handleUploadTicket(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::UploadTicketRsp rsp;

    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kUploadTicketRsp, rsp);
    };

    if (!peer->isLoggedIn()) {
        fail("请先登录");
        return;
    }

    im::UploadTicketReq req;
    if (!parseBody(body, &req)) {
        fail("请求格式错误");
        return;
    }

    if (!cos_.enabled()) {
        fail("COS 未配置，暂时无法上传");
        return;
    }

    if (req.size() == 0 || req.size() > kMaxFileBytes) {
        fail("文件大小需在 5MB 以内");
        return;
    }

    const std::string filename = baseName(req.filename());
    if (filename.empty() || filename.size() > 200) {
        fail("文件名无效");
        return;
    }

    const std::string ext = fileExtension(filename);
    if (!validFileExt(ext)) {
        fail("不支持的文件类型");
        return;
    }

    const std::string content_type =
        req.content_type().empty() ? defaultContentType(ext) : lowerAscii(req.content_type());

    const bool image_ext = (ext == "png" || ext == "jpg" || ext == "jpeg" ||
                            ext == "gif" || ext == "webp");
    if (image_ext && content_type.rfind("image/", 0) != 0) {
        fail("文件类型与后缀不匹配");
        return;
    }

    const std::int64_t now = nowSeconds();
    const std::string key =
        "chat/" + todayKey() + "/" + im::crypto::randomHex(16) + "." + ext;

    CosUploadTicket ticket;
    std::string err;
    if (!buildCosUploadTicket(cos_, key, now, 600, &ticket, &err)) {
        fail(err.empty() ? "生成上传签名失败" : err);
        return;
    }

    rsp.set_success(true);
    rsp.set_upload_url(ticket.upload_url);
    rsp.set_file_url(ticket.file_url);
    rsp.set_key(ticket.key);
    rsp.set_expires_at(now + 600);
    peer->sendProto(im::MsgType::kUploadTicketRsp, rsp);

    spdlog::info("[svc] upload ticket uid={} key={} size={}", peer->uid, key, req.size());
}

// ---------------------------------------------------------------------------
//  动态（朋友圈 / 微博）
// ---------------------------------------------------------------------------
im::PostInfo ChatService::buildPost(std::uint64_t viewer_uid, const PostRow& row) const {
    im::PostInfo info;
    info.set_id(row.id);
    info.set_uid(row.uid);
    info.set_content(row.content);
    info.set_created_at(row.created_at);
    info.set_mine(row.uid == viewer_uid);
    for (const auto& url : row.images) {
        info.add_images(url);
    }

    UserRow author;
    if (db_.findUserById(row.uid, &author)) {
        info.set_nickname(author.nickname);
        info.set_signature(author.signature);
        info.set_avatar_url(author.avatar_url);
    }

    info.set_like_count(db_.likeCount(row.id));
    info.set_liked(db_.hasLiked(row.id, viewer_uid));
    info.set_comment_count(db_.commentCount(row.id));

    for (const auto& comment : db_.commentsOf(row.id, 20)) {
        auto* out = info.add_comments();
        out->set_id(comment.id);
        out->set_post_id(comment.post_id);
        out->set_uid(comment.uid);
        out->set_nickname(comment.nickname);
        out->set_content(comment.content);
        out->set_created_at(comment.created_at);
    }
    return info;
}

void ChatService::handlePostCreate(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::PostCreateRsp rsp;
    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kPostCreateRsp, rsp);
    };

    if (!peer->isLoggedIn()) {
        fail("请先登录");
        return;
    }

    im::PostCreateReq req;
    if (!parseBody(body, &req)) {
        fail("请求格式错误");
        return;
    }

    const std::string content = trim(req.content());
    if (content.size() > 2000) {
        fail("动态内容最多 2000 字");
        return;
    }
    if (req.images_size() > 9) {
        fail("最多只能放 9 张图片");
        return;
    }

    std::vector<std::string> images;
    images.reserve(static_cast<std::size_t>(req.images_size()));
    for (const auto& url : req.images()) {
        if (url.rfind("https://", 0) != 0) {
            fail("图片地址无效");
            return;
        }
        if (cos_.enabled() && url.find(cos_.host) == std::string::npos) {
            fail("图片地址不属于本服务");
            return;
        }
        images.push_back(url);
    }

    if (content.empty() && images.empty()) {
        fail("写点什么或者选张图片吧");
        return;
    }

    const std::int64_t ts = nowSeconds();
    const std::uint64_t post_id = db_.createPost(peer->uid, content, images, ts);
    if (post_id == 0) {
        fail("发布失败，请稍后重试");
        return;
    }

    PostRow row;
    row.id = post_id;
    row.uid = peer->uid;
    row.content = content;
    row.images = images;
    row.created_at = ts;

    rsp.set_success(true);
    *rsp.mutable_post() = buildPost(peer->uid, row);
    peer->sendProto(im::MsgType::kPostCreateRsp, rsp);

    // 在线好友收到实时推送（朋友圈式：只推给好友）
    for (const auto& friendRow : db_.friendsOf(peer->uid)) {
        auto target = findOnline(friendRow.uid);
        if (!target) continue;
        target->sendProto(im::MsgType::kPostPush, buildPost(friendRow.uid, row));
    }

    spdlog::info("[svc] post {} created by {} ({} images)", post_id, peer->uid, images.size());
}

void ChatService::handleFeed(const std::shared_ptr<Peer>& peer, const std::string& body) {
    if (!peer->isLoggedIn()) {
        sendError(peer, 401, "请先登录");
        return;
    }

    im::FeedReq req;
    if (!parseBody(body, &req)) {
        sendError(peer, 400, "请求格式错误");
        return;
    }

    std::uint32_t limit = req.limit() == 0 ? 20u : req.limit();
    if (limit > 50) limit = 50;

    bool has_more = false;
    const auto rows = db_.feed(peer->uid, req.author_uid(), req.before_id(), limit, &has_more);

    im::FeedRsp rsp;
    rsp.set_has_more(has_more);
    rsp.set_author_uid(req.author_uid());
    for (const auto& row : rows) {
        *rsp.add_posts() = buildPost(peer->uid, row);
    }
    peer->sendProto(im::MsgType::kFeedRsp, rsp);
}

void ChatService::handlePostLike(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::PostLikeRsp rsp;
    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kPostLikeRsp, rsp);
    };

    if (!peer->isLoggedIn()) {
        fail("请先登录");
        return;
    }

    im::PostLikeReq req;
    if (!parseBody(body, &req)) {
        fail("请求格式错误");
        return;
    }
    if (!db_.canViewPost(req.post_id(), peer->uid)) {
        fail("动态不存在或无权查看");
        return;
    }

    db_.setPostLike(req.post_id(), peer->uid, req.like());

    rsp.set_success(true);
    rsp.set_post_id(req.post_id());
    rsp.set_liked(db_.hasLiked(req.post_id(), peer->uid));
    rsp.set_like_count(db_.likeCount(req.post_id()));
    peer->sendProto(im::MsgType::kPostLikeRsp, rsp);

    // 点赞时通知作者（取消赞不通知，自己赞自己不通知）
    PostRow post;
    if (req.like() && db_.getPost(req.post_id(), &post) && post.uid != peer->uid) {
        if (auto author = findOnline(post.uid)) {
            im::PostNotice notice;
            notice.set_kind(1);
            notice.set_post_id(post.id);
            notice.set_from_uid(peer->uid);
            notice.set_from_nickname(peer->nickname);
            notice.set_created_at(nowSeconds());
            notice.set_like_count(rsp.like_count());
            author->sendProto(im::MsgType::kPostNotice, notice);
        }
    }
}

void ChatService::handleCommentCreate(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::CommentCreateRsp rsp;
    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kCommentCreateRsp, rsp);
    };

    if (!peer->isLoggedIn()) {
        fail("请先登录");
        return;
    }

    im::CommentCreateReq req;
    if (!parseBody(body, &req)) {
        fail("请求格式错误");
        return;
    }

    const std::string content = trim(req.content());
    if (content.empty() || content.size() > 500) {
        fail("评论内容需在 1~500 字之间");
        return;
    }
    if (!db_.canViewPost(req.post_id(), peer->uid)) {
        fail("动态不存在或无权查看");
        return;
    }

    const std::int64_t ts = nowSeconds();
    const std::uint64_t comment_id = db_.addComment(req.post_id(), peer->uid, content, ts);
    if (comment_id == 0) {
        fail("评论失败，请稍后重试");
        return;
    }

    rsp.set_success(true);
    auto* comment = rsp.mutable_comment();
    comment->set_id(comment_id);
    comment->set_post_id(req.post_id());
    comment->set_uid(peer->uid);
    comment->set_nickname(peer->nickname);
    comment->set_content(content);
    comment->set_created_at(ts);
    rsp.set_comment_count(db_.commentCount(req.post_id()));
    peer->sendProto(im::MsgType::kCommentCreateRsp, rsp);

    // 评论通知作者
    PostRow post;
    if (db_.getPost(req.post_id(), &post) && post.uid != peer->uid) {
        if (auto author = findOnline(post.uid)) {
            im::PostNotice notice;
            notice.set_kind(2);
            notice.set_post_id(post.id);
            notice.set_from_uid(peer->uid);
            notice.set_from_nickname(peer->nickname);
            notice.set_content(content);
            notice.set_created_at(ts);
            notice.set_comment_count(rsp.comment_count());
            author->sendProto(im::MsgType::kPostNotice, notice);
        }
    }
}

void ChatService::handlePostDelete(const std::shared_ptr<Peer>& peer, const std::string& body) {
    im::PostDeleteRsp rsp;
    auto fail = [&](const std::string& message) {
        rsp.set_success(false);
        rsp.set_err_msg(message);
        peer->sendProto(im::MsgType::kPostDeleteRsp, rsp);
    };

    if (!peer->isLoggedIn()) {
        fail("请先登录");
        return;
    }

    im::PostDeleteReq req;
    if (!parseBody(body, &req)) {
        fail("请求格式错误");
        return;
    }

    PostRow row;
    if (!db_.getPost(req.post_id(), &row)) {
        fail("动态不存在");
        return;
    }
    if (row.uid != peer->uid) {
        fail("只能删除自己的动态");
        return;
    }

    if (!db_.deletePost(req.post_id(), peer->uid)) {
        fail("删除失败");
        return;
    }

    rsp.set_success(true);
    rsp.set_post_id(req.post_id());
    peer->sendProto(im::MsgType::kPostDeleteRsp, rsp);
    spdlog::info("[svc] post {} deleted by {}", req.post_id(), peer->uid);
}

// ---------------------------------------------------------------------------
//  推送
// ---------------------------------------------------------------------------
void ChatService::broadcastOnline() {
    im::OnlineUsersPush push;
    for (const auto& user : db_.allUsers()) {
        *push.add_users() = makeUserInfo(user, findOnline(user.uid) != nullptr, false);
    }

    for (auto it = online_.begin(); it != online_.end();) {
        if (auto peer = it->second.lock()) {
            peer->sendProto(im::MsgType::kOnlineUsersPush, push);
            ++it;
        } else {
            it = online_.erase(it);
        }
    }
}

void ChatService::pushFriendList(std::uint64_t uid) {
    auto peer = findOnline(uid);
    if (!peer) return;

    im::FriendListPush push;
    for (const auto& user : db_.friendsOf(uid)) {
        *push.add_friends() = makeUserInfo(user, findOnline(user.uid) != nullptr, true);
    }
    peer->sendProto(im::MsgType::kFriendListPush, push);
}

void ChatService::pushGroupList(std::uint64_t uid) {
    auto peer = findOnline(uid);
    if (!peer) return;

    im::GroupListPush push;
    for (const auto& group : db_.groupsOf(uid)) {
        *push.add_groups() = makeGroupInfo(group);
    }
    peer->sendProto(im::MsgType::kGroupListPush, push);
}

void ChatService::pushGroupListsOf(std::uint64_t gid) {
    for (std::uint64_t uid : memberIds(gid)) {
        pushGroupList(uid);
        pushConversations(uid);
    }
}

void ChatService::pushConversations(std::uint64_t uid) {
    auto peer = findOnline(uid);
    if (!peer) return;

    im::ConversationListPush push;

    for (const auto& row : db_.privateConversations(uid)) {
        auto* conv = push.add_conversations();
        conv->set_kind(1);
        conv->set_peer_uid(row.peer_uid);
        conv->set_nickname(row.nickname);
        conv->set_online(findOnline(row.peer_uid) != nullptr);
        conv->set_last_content(row.last_content);
        conv->set_last_time(row.last_time);
        conv->set_unread(row.unread);
    }

    for (const auto& row : db_.groupConversations(uid)) {
        auto* conv = push.add_conversations();
        conv->set_kind(2);
        conv->set_gid(row.gid);
        conv->set_nickname(row.name);
        conv->set_online(false);
        conv->set_last_content(row.last_content);
        conv->set_last_time(row.last_time);
        conv->set_unread(row.unread);
        conv->set_last_sender(row.last_sender);
    }

    peer->sendProto(im::MsgType::kConversationListPush, push);
}

void ChatService::pushPendingRequests(std::uint64_t uid) {
    auto peer = findOnline(uid);
    if (!peer) return;

    im::FriendRequestListRsp rsp;
    for (const auto& request : db_.pendingRequestsFor(uid)) {
        UserRow from;
        if (!db_.findUserById(request.from_uid, &from)) continue;

        auto* info = rsp.add_requests();
        info->set_request_id(request.id);
        info->set_from_uid(request.from_uid);
        info->set_from_nickname(from.nickname);
        info->set_from_pp_id(from.pp_id);
        info->set_message(request.message);
        info->set_created_at(request.created_at);
        info->set_status(request.status);
    }
    peer->sendProto(im::MsgType::kFriendRequestListRsp, rsp);
}

void ChatService::pushFriendRequestTo(std::uint64_t uid, std::uint64_t request_id, std::uint32_t status) {
    auto peer = findOnline(uid);
    if (!peer) return;

    FriendRequestRow request;
    if (!db_.getFriendRequest(request_id, &request)) return;

    UserRow from;
    db_.findUserById(request.from_uid, &from);

    im::FriendRequestPush push;
    auto* info = push.mutable_request();
    info->set_request_id(request.id);
    info->set_from_uid(request.from_uid);
    info->set_from_nickname(from.nickname);
    info->set_from_pp_id(from.pp_id);
    info->set_message(request.message);
    info->set_created_at(request.created_at);
    info->set_status(status != 0 ? status : request.status);

    peer->sendProto(im::MsgType::kFriendRequestPush, push);
}

void ChatService::pushRequestStatusToRequester(const FriendRequestRow& request) {
    pushFriendRequestTo(request.from_uid, request.id, request.status);
}

void ChatService::sendSystemMessage(std::uint64_t gid, const std::string& content, std::int64_t ts) {
    const std::uint64_t msg_id = db_.insertGroupMessage(gid, 0, content, 2, ts);
    if (msg_id == 0) return;

    im::ChatGroup out;
    out.set_gid(gid);
    out.set_from_uid(0);
    out.set_content(content);
    out.set_timestamp(ts);
    out.set_msg_id(msg_id);
    out.set_kind(2);

    for (std::uint64_t uid : memberIds(gid)) {
        if (auto target = findOnline(uid)) {
            target->sendProto(im::MsgType::kChatGroup, out);
        }
    }
}

// ---------------------------------------------------------------------------
//  工具
// ---------------------------------------------------------------------------
void ChatService::sendOp(const std::shared_ptr<Peer>& peer, im::MsgType type, bool success, const std::string& message) {
    im::OpResult result;
    result.set_success(success);
    result.set_err_msg(message);
    peer->sendProto(type, result);
}

void ChatService::sendError(const std::shared_ptr<Peer>& peer, int code, const std::string& message) {
    im::ErrorPush err;
    err.set_code(code);
    err.set_message(message);
    peer->sendProto(im::MsgType::kErrorPush, err);
}

std::shared_ptr<Peer> ChatService::findOnline(std::uint64_t uid) const {
    auto it = online_.find(uid);
    if (it == online_.end()) return nullptr;
    return it->second.lock();
}

std::vector<std::uint64_t> ChatService::memberIds(std::uint64_t gid) const {
    std::vector<std::uint64_t> ids;
    for (const auto& member : db_.groupMembers(gid)) {
        ids.push_back(member.uid);
    }
    return ids;
}

}  // namespace im::server
