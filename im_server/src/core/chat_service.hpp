// =============================================================================
//  im_server/src/core/chat_service.hpp
//  业务层：账号 / 资料 / 好友 / 私聊 / 群聊 / 在线状态。
//
//  只依赖 Peer 接口，不认识 WebSocket 也不认识长度头。
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cos.hpp"
#include "core/database.hpp"
#include "core/peer.hpp"
#include "im_msg.pb.h"

namespace im::server {

class ChatService {
public:
    ChatService(Database& db, CosConfig cos) : db_(db), cos_(std::move(cos)) {}

    void onOpen(const std::shared_ptr<Peer>& peer);
    void onClose(const std::shared_ptr<Peer>& peer);
    void onPacket(const std::shared_ptr<Peer>& peer, std::uint32_t type, const std::string& body);

    // ---- 供 HTTP 层（OAuth 授权页）复用，避免两套密码校验逻辑 ----
    // 账号密码校验：用户名 / pp 号都可以，成功时填 out
    bool verifyCredentials(const std::string& account, const std::string& password, UserRow* out) const;
    // 签发 pp 会话令牌（默认 7 天）
    std::string issueSessionToken(std::uint64_t uid);
    // 令牌是否有效；有效时填 out
    bool resolveSessionToken(const std::string& token, UserRow* out) const;
    // 登出：删掉该令牌
    void revokeSessionToken(const std::string& token);

    // ---- OAuth 2.0（授权码模式）----
    // 注册 / 更新接入方（启动时用环境变量灌入，幂等）
    bool upsertOauthClient(const std::string& client_id,
                           const std::string& client_secret,
                           const std::string& name,
                           const std::string& redirect_uris);
    // 用户同意授权：校验 client 与 redirect_uri，签发一次性 code 并拼好回跳地址
    bool oauthAuthorize(const std::string& client_id,
                        const std::string& redirect_uri,
                        const std::string& state,
                        const std::string& scope,
                        std::uint64_t uid,
                        std::string* redirect_url,
                        std::string* err);
    // 校捷通服务端用 code 换 access_token
    bool oauthToken(const std::string& grant_type,
                    const std::string& code,
                    const std::string& client_id,
                    const std::string& client_secret,
                    const std::string& redirect_uri,
                    im::OAuthTokenRsp* rsp,
                    std::string* err);
    // 用 access_token 取用户资料
    bool oauthUserInfo(const std::string& access_token, im::OAuthUserInfoRsp* rsp, std::string* err);
    // 授权管理：我授权过的应用
    void listGrants(std::uint64_t uid, im::OAuthGrantListRsp* rsp);
    // 授权管理：解除授权（返回实际删除的令牌数）
    std::uint32_t revokeGrant(std::uint64_t uid, const std::string& client_id);
    // 接入方服务端：撤销某个 pp 用户对本应用的授权（解绑联动）
    bool oauthRevokeForClient(const std::string& client_id,
                              const std::string& client_secret,
                              std::uint64_t uid,
                              std::uint32_t* revoked,
                              std::string* err);

private:
    // ---- 账号 / 资料 ----
    void handleRegister(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleLogin(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleSessionLogin(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleProfileUpdate(const std::shared_ptr<Peer>& peer, const std::string& body);

    // 把连接绑定到用户（踢掉同一账号的旧连接）
    void attachUser(const std::shared_ptr<Peer>& peer, const UserRow& user);
    // 登录成功后统一推送在线状态 / 好友 / 群 / 会话 / 待处理申请
    void afterLogin(std::uint64_t uid);

    // ---- 好友 ----
    void handleFriendSearch(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleFriendRequest(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleFriendRequestList(const std::shared_ptr<Peer>& peer);
    void handleFriendRequestHandle(const std::shared_ptr<Peer>& peer, const std::string& body);

    // ---- 私聊 ----
    void handleChatPrivate(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleHistory(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleRead(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleTyping(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleHeartbeat(const std::shared_ptr<Peer>& peer);

    // ---- 群聊 ----
    void handleGroupCreate(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleGroupInvite(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleGroupQuit(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleGroupMemberList(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleChatGroup(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleGroupHistory(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleGroupRead(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleUploadTicket(const std::shared_ptr<Peer>& peer, const std::string& body);

    // ---- 收藏夹 ----
    void handleFavoriteAdd(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleFavoriteList(const std::shared_ptr<Peer>& peer);
    void handleFavoriteRemove(const std::shared_ptr<Peer>& peer, const std::string& body);

    // ---- 撤回 ----
    void handleRecall(const std::shared_ptr<Peer>& peer, const std::string& body);

    // ---- 拍一拍（双击头像）----
    void handlePat(const std::shared_ptr<Peer>& peer, const std::string& body);

    // ---- 动态（朋友圈 / 微博）----
    void handlePostCreate(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleFeed(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handlePostLike(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handleCommentCreate(const std::shared_ptr<Peer>& peer, const std::string& body);
    void handlePostDelete(const std::shared_ptr<Peer>& peer, const std::string& body);
    im::PostInfo buildPost(std::uint64_t viewer_uid, const PostRow& row) const;

    // ---- 推送 ----
    void broadcastOnline();
    void pushFriendList(std::uint64_t uid);
    void pushGroupList(std::uint64_t uid);
    void pushGroupListsOf(std::uint64_t gid);
    void pushConversations(std::uint64_t uid);
    void pushPendingRequests(std::uint64_t uid);
    void pushFriendRequestTo(std::uint64_t uid, std::uint64_t request_id, std::uint32_t status);
    void pushRequestStatusToRequester(const FriendRequestRow& request);

    // ---- 工具 ----
    void sendOp(const std::shared_ptr<Peer>& peer, im::MsgType type, bool success, const std::string& message);
    void sendError(const std::shared_ptr<Peer>& peer, int code, const std::string& message);
    void sendSystemMessage(std::uint64_t gid, const std::string& content, std::int64_t ts);
    std::shared_ptr<Peer> findOnline(std::uint64_t uid) const;
    std::vector<std::uint64_t> memberIds(std::uint64_t gid) const;

    Database& db_;
    CosConfig cos_;
    std::unordered_map<std::uint64_t, std::weak_ptr<Peer>> online_;
};

}  // namespace im::server
