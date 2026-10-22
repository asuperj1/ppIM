// =============================================================================
//  im_server/src/core/database.hpp
//  SQLite 持久层：用户 / 好友 / 好友申请 / 私聊 / 群聊 / 已读位点。
//
//  设计说明：
//    - 全部接口同步调用（单 io_context 线程，量级小）
//    - 返回值都是强类型 struct，业务层不碰 sqlite3_stmt
//    - 旧数据库启动时自动迁移（补 pp_id / signature / group_id 等列）
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace im::server {

struct UserRow {
    std::uint64_t uid{};
    std::string username;
    std::string nickname;
    std::string pp_id;
    std::string signature;
    std::string avatar_url;
    std::string salt;
    std::string password_hash;
};

struct MessageRow {
    std::uint64_t id{};
    std::uint64_t from_uid{};
    std::uint64_t to_uid{};
    std::uint64_t group_id{};
    std::string content;
    std::uint32_t kind{1};       // 1 普通消息 2 系统消息
    std::uint32_t msg_kind{1};   // 1 文本 2 图片 3 文件
    std::string file_url;
    std::string file_name;
    std::uint64_t file_size{};
    // 引用回复（微信式）：quote_text 是引用原文摘要，空串表示未引用
    std::string quote_id;
    std::string quote_text;
    std::string quote_nickname;
    // 已撤回：服务端已清空正文与附件，前端只显示「xxx撤回了一条消息」
    bool recalled{false};
    std::int64_t created_at{};
};

struct FavoriteRow {
    std::string msg_id;
    std::uint32_t msg_kind{1};
    std::string content;
    std::string file_url;
    std::string file_name;
    std::uint64_t file_size{};
    std::string from_nickname;
    std::int64_t timestamp{};    // 原消息时间
    std::int64_t created_at{};   // 收藏时间
};

struct ConversationRow {
    std::uint64_t peer_uid{};
    std::string nickname;
    std::string last_content;
    std::int64_t last_time{};
    std::uint32_t unread{};
};

struct GroupConversationRow {
    std::uint64_t gid{};
    std::string name;
    std::string last_content;
    std::string last_sender;
    std::int64_t last_time{};
    std::uint32_t unread{};
};

struct FriendRequestRow {
    std::uint64_t id{};
    std::uint64_t from_uid{};
    std::uint64_t to_uid{};
    std::string message;
    std::uint32_t status{0};   // 0 待处理 1 已接受 2 已拒绝
    std::int64_t created_at{};
};

struct GroupRow {
    std::uint64_t gid{};
    std::string name;
    std::uint64_t owner_uid{};
    std::uint32_t member_count{};
    std::int64_t created_at{};
};

struct GroupMemberRow {
    std::uint64_t gid{};
    std::uint64_t uid{};
    std::uint32_t role{0};   // 1 群主 0 成员
    std::string nickname;
    std::string pp_id;
    std::int64_t joined_at{};
};

struct PostRow {
    std::uint64_t id{};
    std::uint64_t uid{};
    std::string content;
    std::vector<std::string> images;
    std::int64_t created_at{};
};

struct PostCommentRow {
    std::uint64_t id{};
    std::uint64_t post_id{};
    std::uint64_t uid{};
    std::string nickname;
    std::string content;
    std::int64_t created_at{};
};

// OAuth 接入方（校捷通等）。redirect_uris 是逗号分隔的白名单。
struct OauthClientRow {
    std::string client_id;
    std::string client_secret_hash;   // SHA-256(client_secret)
    std::string name;
    std::string redirect_uris;
    std::int64_t created_at{};
};

// 统一令牌表的一行。kind: 1 = pp 会话，2 = 授权码，3 = OAuth access_token
struct AuthTokenRow {
    std::uint32_t kind{};
    std::uint64_t uid{};
    std::string client_id;
    std::string redirect_uri;
    std::int64_t expires_at{};
};

// 授权管理列表项（按 client_id 聚合）
struct OauthGrantRow {
    std::string client_id;
    std::string name;
    std::int64_t last_authorized_at{};
    std::uint32_t active_tokens{};
    std::string scope;
};

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& path);
    void close();

    // ---------------------------------- 用户 ----------------------------------
    bool createUser(const std::string& username,
                    const std::string& nickname,
                    const std::string& salt,
                    const std::string& password_hash,
                    std::uint64_t* uid,
                    std::string* err);

    bool findUserByName(const std::string& username, UserRow* out) const;
    bool findUserById(std::uint64_t uid, UserRow* out) const;
    bool findUserByPpId(const std::string& pp_id, UserRow* out) const;
    std::vector<UserRow> allUsers() const;
    std::vector<UserRow> searchUsers(const std::string& keyword, std::uint64_t exclude_uid, std::uint32_t limit) const;
    bool updateProfile(std::uint64_t uid,
                       const std::string& nickname,
                       const std::string& signature,
                       const std::string& pp_id,
                       std::string* err,
                       const std::string& avatar_url = {});

    // ---------------------------------- 好友 ----------------------------------
    std::vector<UserRow> friendsOf(std::uint64_t uid) const;
    bool areFriends(std::uint64_t a, std::uint64_t b) const;
    void addFriendPair(std::uint64_t a, std::uint64_t b);

    bool hasPendingRequest(std::uint64_t from_uid, std::uint64_t to_uid) const;
    bool findPendingRequestBetween(std::uint64_t a, std::uint64_t b, std::uint64_t* request_id) const;
    std::uint64_t createFriendRequest(std::uint64_t from_uid,
                                      std::uint64_t to_uid,
                                      const std::string& message,
                                      std::int64_t created_at);
    std::vector<FriendRequestRow> pendingRequestsFor(std::uint64_t uid) const;
    bool getFriendRequest(std::uint64_t request_id, FriendRequestRow* out) const;
    void setFriendRequestStatus(std::uint64_t request_id, std::uint32_t status);

    // ---------------------------------- 私聊 ----------------------------------
    std::uint64_t insertMessage(std::uint64_t from_uid,
                                std::uint64_t to_uid,
                                const std::string& content,
                                std::int64_t created_at,
                                std::uint32_t msg_kind = 1,
                                const std::string& file_url = {},
                                const std::string& file_name = {},
                                std::uint64_t file_size = 0,
                                const std::string& quote_id = {},
                                const std::string& quote_text = {},
                                const std::string& quote_nickname = {});

    std::vector<MessageRow> history(std::uint64_t uid,
                                    std::uint64_t peer_uid,
                                    std::uint64_t before_id,
                                    std::uint32_t limit,
                                    bool* has_more) const;

    // 按 ID 取一条消息（撤回时校验归属与时间）
    bool getMessage(std::uint64_t msg_id, MessageRow* out) const;
    // 撤回：只能撤回自己发的且未撤回过的消息，同时清空正文与附件
    bool recallMessage(std::uint64_t msg_id, std::uint64_t from_uid);

    std::vector<ConversationRow> privateConversations(std::uint64_t uid) const;

    std::uint32_t unreadCount(std::uint64_t uid, std::uint64_t peer_uid) const;
    void markRead(std::uint64_t uid, std::uint64_t peer_uid);

    // ---------------------------------- 群聊 ----------------------------------
    std::uint64_t createGroup(const std::string& name,
                              std::uint64_t owner_uid,
                              const std::vector<std::uint64_t>& members,
                              std::int64_t created_at);
    bool groupExists(std::uint64_t gid) const;
    bool getGroup(std::uint64_t gid, GroupRow* out) const;
    std::vector<GroupRow> groupsOf(std::uint64_t uid) const;
    std::vector<GroupMemberRow> groupMembers(std::uint64_t gid) const;
    bool isGroupMember(std::uint64_t gid, std::uint64_t uid) const;
    std::uint32_t groupMemberCount(std::uint64_t gid) const;
    void addGroupMember(std::uint64_t gid, std::uint64_t uid, std::uint32_t role, std::int64_t joined_at);
    void removeGroupMember(std::uint64_t gid, std::uint64_t uid);
    void deleteGroup(std::uint64_t gid);

    std::uint64_t insertGroupMessage(std::uint64_t gid,
                                     std::uint64_t from_uid,
                                     const std::string& content,
                                     std::uint32_t kind,
                                     std::int64_t created_at,
                                     std::uint32_t msg_kind = 1,
                                     const std::string& file_url = {},
                                     const std::string& file_name = {},
                                    std::uint64_t file_size = 0,
                                    const std::string& quote_id = {},
                                    const std::string& quote_text = {},
                                    const std::string& quote_nickname = {});

    std::vector<MessageRow> groupHistory(std::uint64_t gid,
                                         std::uint64_t before_id,
                                         std::uint32_t limit,
                                         bool* has_more) const;

    std::vector<GroupConversationRow> groupConversations(std::uint64_t uid) const;
    std::uint32_t groupUnread(std::uint64_t uid, std::uint64_t gid) const;
    void markGroupRead(std::uint64_t uid, std::uint64_t gid);

    // ---------------------------------- 动态 ----------------------------------
    std::uint64_t createPost(std::uint64_t uid,
                             const std::string& content,
                             const std::vector<std::string>& images,
                             std::int64_t created_at);
    bool getPost(std::uint64_t post_id, PostRow* out) const;
    std::vector<PostRow> feed(std::uint64_t viewer_uid,
                              std::uint64_t author_uid,
                              std::uint64_t before_id,
                              std::uint32_t limit,
                              bool* has_more) const;
    bool deletePost(std::uint64_t post_id, std::uint64_t uid);
    bool canViewPost(std::uint64_t post_id, std::uint64_t uid) const;

    bool setPostLike(std::uint64_t post_id, std::uint64_t uid, bool like);
    std::uint32_t likeCount(std::uint64_t post_id) const;
    bool hasLiked(std::uint64_t post_id, std::uint64_t uid) const;

    std::uint64_t addComment(std::uint64_t post_id,
                             std::uint64_t uid,
                             const std::string& content,
                             std::int64_t created_at);
    std::vector<PostCommentRow> commentsOf(std::uint64_t post_id, std::uint32_t limit) const;
    std::uint32_t commentCount(std::uint64_t post_id) const;

    // ----------------------------- OAuth / 会话令牌 ---------------------------
    // 明文 token 只返回给调用方一次，库里只存 SHA-256(token)；泄露库也拿不到可用令牌。
    std::string createAuthToken(std::uint32_t kind,
                                std::uint64_t uid,
                                const std::string& client_id,
                                const std::string& redirect_uri,
                                std::int64_t expires_at,
                                const std::string& scope = "profile");
    bool findAuthToken(const std::string& token, std::uint32_t kind, AuthTokenRow* out) const;
    // 原子消费：只有把 used 从 0 改成 1 的那一次调用会成功（防授权码重放）
    bool consumeAuthToken(const std::string& token, std::uint32_t kind, AuthTokenRow* out);
    void deleteAuthToken(const std::string& token);
    void purgeExpiredAuthTokens(std::int64_t now);

    bool upsertOauthClient(const OauthClientRow& row);
    bool getOauthClient(const std::string& client_id, OauthClientRow* out) const;

    // 授权管理：我授权过的应用（按最近授权时间倒序）
    std::vector<OauthGrantRow> grantsOf(std::uint64_t uid) const;
    // 解除授权：删除该用户在该应用下的全部 access_token，返回删除数量
    std::uint32_t revokeGrants(std::uint64_t uid, const std::string& client_id);

    // ---------------------------------- 收藏 ----------------------------------
    // 收藏是消息副本：原消息删除后收藏仍然在（与微信一致）
    bool addFavorite(std::uint64_t uid, const FavoriteRow& row);
    std::vector<FavoriteRow> favoritesOf(std::uint64_t uid) const;
    bool removeFavorite(std::uint64_t uid, const std::string& msg_id);

private:
    bool migrate();
    bool hasColumn(const char* table, const char* column) const;
    void exec(const char* sql) const;

    sqlite3* db_{nullptr};
};

}  // namespace im::server
