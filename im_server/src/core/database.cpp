// =============================================================================
//  im_server/src/core/database.cpp
// =============================================================================
#include "core/database.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "im/common/crypto.hpp"

namespace im::server {
namespace {

struct Stmt {
    sqlite3_stmt* raw{nullptr};
    ~Stmt() {
        if (raw != nullptr) {
            sqlite3_finalize(raw);
        }
    }
};

bool prepare(sqlite3* db, const char* sql, Stmt* out) {
    if (sqlite3_prepare_v2(db, sql, -1, &out->raw, nullptr) != SQLITE_OK) {
        spdlog::error("[db] prepare failed: {} | SQL: {}", sqlite3_errmsg(db), sql);
        return false;
    }
    return true;
}

std::string columnText(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    return text == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(text));
}

void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void bindU64(sqlite3_stmt* stmt, int index, std::uint64_t value) {
    sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value));
}

std::string autoPpId(std::uint64_t uid) {
    return "pp" + std::to_string(100000ull + uid);
}

UserRow readUser(sqlite3_stmt* stmt, int base) {
    UserRow row;
    row.uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, base + 0));
    row.username = columnText(stmt, base + 1);
    row.nickname = columnText(stmt, base + 2);
    row.pp_id = columnText(stmt, base + 3);
    row.signature = columnText(stmt, base + 4);
    row.avatar_url = columnText(stmt, base + 5);
    return row;
}

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS users (
    uid           INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,
    nickname      TEXT    NOT NULL,
    pp_id         TEXT    NOT NULL DEFAULT '',
    signature     TEXT    NOT NULL DEFAULT '',
    avatar_url    TEXT    NOT NULL DEFAULT '',
    salt          TEXT    NOT NULL,
    password_hash TEXT    NOT NULL,
    created_at    INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS messages (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    from_uid   INTEGER NOT NULL,
    to_uid     INTEGER NOT NULL,
    group_id   INTEGER NOT NULL DEFAULT 0,
    content    TEXT    NOT NULL,
    kind       INTEGER NOT NULL DEFAULT 1,
    msg_kind   INTEGER NOT NULL DEFAULT 1,
    file_url   TEXT    NOT NULL DEFAULT '',
    file_name  TEXT    NOT NULL DEFAULT '',
    file_size  INTEGER NOT NULL DEFAULT 0,
    quote_id   TEXT    NOT NULL DEFAULT '',
    quote_text TEXT    NOT NULL DEFAULT '',
    quote_nickname TEXT NOT NULL DEFAULT '',
    recalled   INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
);

-- 收藏夹：每条记录是一份消息副本，与 messages 解耦（原消息删了收藏仍在）
CREATE TABLE IF NOT EXISTS favorites (
    uid           INTEGER NOT NULL,
    msg_id        TEXT    NOT NULL,
    msg_kind      INTEGER NOT NULL DEFAULT 1,
    content       TEXT    NOT NULL DEFAULT '',
    file_url      TEXT    NOT NULL DEFAULT '',
    file_name     TEXT    NOT NULL DEFAULT '',
    file_size     INTEGER NOT NULL DEFAULT 0,
    from_nickname TEXT    NOT NULL DEFAULT '',
    ts            INTEGER NOT NULL DEFAULT 0,
    created_at    INTEGER NOT NULL,
    PRIMARY KEY (uid, msg_id)
);

CREATE TABLE IF NOT EXISTS read_state (
    uid          INTEGER NOT NULL,
    peer_uid     INTEGER NOT NULL,
    last_read_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (uid, peer_uid)
);

CREATE TABLE IF NOT EXISTS friendships (
    uid         INTEGER NOT NULL,
    peer_uid    INTEGER NOT NULL,
    created_at  INTEGER NOT NULL,
    PRIMARY KEY (uid, peer_uid)
);

CREATE TABLE IF NOT EXISTS friend_requests (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    from_uid   INTEGER NOT NULL,
    to_uid     INTEGER NOT NULL,
    message    TEXT    NOT NULL DEFAULT '',
    status     INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS chat_groups (
    gid        INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL,
    owner_uid  INTEGER NOT NULL,
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS group_members (
    gid       INTEGER NOT NULL,
    uid       INTEGER NOT NULL,
    role      INTEGER NOT NULL DEFAULT 0,
    joined_at INTEGER NOT NULL,
    PRIMARY KEY (gid, uid)
);

CREATE TABLE IF NOT EXISTS group_read_state (
    uid          INTEGER NOT NULL,
    gid          INTEGER NOT NULL,
    last_read_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (uid, gid)
);

CREATE TABLE IF NOT EXISTS posts (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    uid        INTEGER NOT NULL,
    content    TEXT    NOT NULL DEFAULT '',
    images     TEXT    NOT NULL DEFAULT '',
    deleted    INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS post_likes (
    post_id    INTEGER NOT NULL,
    uid        INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (post_id, uid)
);

CREATE TABLE IF NOT EXISTS post_comments (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    post_id    INTEGER NOT NULL,
    uid        INTEGER NOT NULL,
    content    TEXT    NOT NULL,
    created_at INTEGER NOT NULL
);

-- OAuth 接入方（校捷通等站点）
CREATE TABLE IF NOT EXISTS oauth_clients (
    client_id          TEXT PRIMARY KEY,
    client_secret_hash TEXT    NOT NULL,
    name               TEXT    NOT NULL DEFAULT '',
    redirect_uris      TEXT    NOT NULL DEFAULT '',   -- 逗号分隔的白名单
    created_at         INTEGER NOT NULL
);

-- 统一令牌表：pp 会话令牌 / OAuth 授权码 / OAuth access_token
CREATE TABLE IF NOT EXISTS auth_tokens (
    token_hash   TEXT PRIMARY KEY,                    -- SHA-256(明文 token)
    kind         INTEGER NOT NULL,                   -- 1 会话 2 授权码 3 access_token
    uid          INTEGER NOT NULL DEFAULT 0,
    client_id    TEXT    NOT NULL DEFAULT '',
    redirect_uri TEXT    NOT NULL DEFAULT '',
    scope        TEXT    NOT NULL DEFAULT 'profile', -- 授权范围（空格分隔）
    expires_at   INTEGER NOT NULL,
    used         INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL
);
)sql";

// 索引必须等老库补完列之后再建（否则 messages.group_id 可能还不存在）
constexpr const char* kIndexes = R"sql(
CREATE INDEX IF NOT EXISTS idx_messages_from     ON messages(from_uid, id);
CREATE INDEX IF NOT EXISTS idx_messages_to       ON messages(to_uid, id);
CREATE INDEX IF NOT EXISTS idx_messages_group    ON messages(group_id, id);
CREATE INDEX IF NOT EXISTS idx_friend_requests_to ON friend_requests(to_uid, status);
CREATE INDEX IF NOT EXISTS idx_posts_uid        ON posts(uid, id);
CREATE INDEX IF NOT EXISTS idx_post_comments    ON post_comments(post_id, id);
CREATE INDEX IF NOT EXISTS idx_post_likes       ON post_likes(post_id);
CREATE INDEX IF NOT EXISTS idx_auth_tokens_exp  ON auth_tokens(expires_at);
CREATE INDEX IF NOT EXISTS idx_auth_tokens_uid  ON auth_tokens(uid);
)sql";

}  // namespace

Database::~Database() {
    close();
}

bool Database::open(const std::string& path) {
    close();

    std::error_code ec;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }

    const int rc = sqlite3_open_v2(path.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("[db] open {} failed: {}", path, db_ ? sqlite3_errmsg(db_) : "unknown");
        close();
        return false;
    }

    char* err = nullptr;
    if (sqlite3_exec(db_, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;", nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::warn("[db] pragma failed: {}", err ? err : "unknown");
        sqlite3_free(err);
        err = nullptr;
    }

    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::error("[db] create schema failed: {}", err ? err : "unknown");
        sqlite3_free(err);
        close();
        return false;
    }

    if (!migrate()) {
        close();
        return false;
    }

    if (sqlite3_exec(db_, kIndexes, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::warn("[db] create indexes failed: {}", err ? err : "unknown");
        sqlite3_free(err);
        err = nullptr;
    }

    spdlog::info("[db] opened {}", path);
    return true;
}

void Database::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::exec(const char* sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::warn("[db] exec failed: {} | SQL: {}", err ? err : "unknown", sql);
        sqlite3_free(err);
    }
}

bool Database::hasColumn(const char* table, const char* column) const {
    if (db_ == nullptr) {
        return false;
    }

    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name != nullptr && column != nullptr && std::string(reinterpret_cast<const char*>(name)) == column) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::migrate() {
    // 老库补列（新库在 kSchema 里已经包含）
    if (!hasColumn("users", "pp_id")) {
        exec("ALTER TABLE users ADD COLUMN pp_id TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("users", "signature")) {
        exec("ALTER TABLE users ADD COLUMN signature TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("users", "avatar_url")) {
        exec("ALTER TABLE users ADD COLUMN avatar_url TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("messages", "group_id")) {
        exec("ALTER TABLE messages ADD COLUMN group_id INTEGER NOT NULL DEFAULT 0");
    }
    if (!hasColumn("messages", "kind")) {
        exec("ALTER TABLE messages ADD COLUMN kind INTEGER NOT NULL DEFAULT 1");
    }
    if (!hasColumn("messages", "msg_kind")) {
        exec("ALTER TABLE messages ADD COLUMN msg_kind INTEGER NOT NULL DEFAULT 1");
    }
    if (!hasColumn("messages", "file_url")) {
        exec("ALTER TABLE messages ADD COLUMN file_url TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("messages", "file_name")) {
        exec("ALTER TABLE messages ADD COLUMN file_name TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("messages", "file_size")) {
        exec("ALTER TABLE messages ADD COLUMN file_size INTEGER NOT NULL DEFAULT 0");
    }
    // 引用回复（微信式）
    if (!hasColumn("messages", "quote_id")) {
        exec("ALTER TABLE messages ADD COLUMN quote_id TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("messages", "quote_text")) {
        exec("ALTER TABLE messages ADD COLUMN quote_text TEXT NOT NULL DEFAULT ''");
    }
    if (!hasColumn("messages", "quote_nickname")) {
        exec("ALTER TABLE messages ADD COLUMN quote_nickname TEXT NOT NULL DEFAULT ''");
    }
    // 撤回标记
    if (!hasColumn("messages", "recalled")) {
        exec("ALTER TABLE messages ADD COLUMN recalled INTEGER NOT NULL DEFAULT 0");
    }
    if (!hasColumn("auth_tokens", "scope")) {
        exec("ALTER TABLE auth_tokens ADD COLUMN scope TEXT NOT NULL DEFAULT 'profile'");
    }

    // 给所有没有 pp 号的用户补一个稳定、唯一的 pp 号：pp100001、pp100002...
    exec("UPDATE users SET pp_id = 'pp' || (100000 + uid) WHERE pp_id IS NULL OR pp_id = ''");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_users_pp_id ON users(pp_id)");

    // 老数据里已经有私聊关系的，自动补成好友，避免升级后无法继续聊天
    exec("INSERT OR IGNORE INTO friendships(uid, peer_uid, created_at) "
         "SELECT from_uid, to_uid, strftime('%s','now') FROM messages "
         "WHERE group_id = 0 AND from_uid <> to_uid");
    exec("INSERT OR IGNORE INTO friendships(uid, peer_uid, created_at) "
         "SELECT to_uid, from_uid, strftime('%s','now') FROM messages "
         "WHERE group_id = 0 AND from_uid <> to_uid");

    return true;
}

// ---------------------------------------------------------------------------
//  用户
// ---------------------------------------------------------------------------
bool Database::createUser(const std::string& username,
                          const std::string& nickname,
                          const std::string& salt,
                          const std::string& password_hash,
                          std::uint64_t* uid,
                          std::string* err) {
    if (db_ == nullptr) {
        if (err) *err = "数据库未打开";
        return false;
    }

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO users(username, nickname, salt, password_hash, created_at, pp_id, signature) "
                       "VALUES(?,?,?,?,strftime('%s','now'),'','')",
                 &stmt)) {
        if (err) *err = "数据库错误";
        return false;
    }

    bindText(stmt.raw, 1, username);
    bindText(stmt.raw, 2, nickname);
    bindText(stmt.raw, 3, salt);
    bindText(stmt.raw, 4, password_hash);

    const int rc = sqlite3_step(stmt.raw);
    if (rc != SQLITE_DONE) {
        if (err) *err = (rc == SQLITE_CONSTRAINT) ? "用户名已被注册" : "数据库写入失败";
        return false;
    }

    const std::uint64_t new_uid = static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));

    Stmt update;
    if (prepare(db_, "UPDATE users SET pp_id = ? WHERE uid = ?", &update)) {
        bindText(update.raw, 1, autoPpId(new_uid));
        bindU64(update.raw, 2, new_uid);
        sqlite3_step(update.raw);
    }

    if (uid != nullptr) {
        *uid = new_uid;
    }
    return true;
}

bool Database::findUserByName(const std::string& username, UserRow* out) const {
    if (db_ == nullptr || out == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT uid, username, nickname, pp_id, signature, avatar_url, salt, password_hash "
                       "FROM users WHERE username = ?", &stmt)) {
        return false;
    }
    bindText(stmt.raw, 1, username);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    *out = readUser(stmt.raw, 0);
    out->salt = columnText(stmt.raw, 6);
    out->password_hash = columnText(stmt.raw, 7);
    return true;
}

bool Database::findUserById(std::uint64_t uid, UserRow* out) const {
    if (db_ == nullptr || out == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT uid, username, nickname, pp_id, signature, avatar_url, salt, password_hash "
                       "FROM users WHERE uid = ?", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, uid);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    *out = readUser(stmt.raw, 0);
    out->salt = columnText(stmt.raw, 6);
    out->password_hash = columnText(stmt.raw, 7);
    return true;
}

bool Database::findUserByPpId(const std::string& pp_id, UserRow* out) const {
    if (db_ == nullptr || out == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT uid, username, nickname, pp_id, signature, avatar_url, salt, password_hash "
                       "FROM users WHERE pp_id = ? COLLATE NOCASE",
                 &stmt)) {
        return false;
    }
    bindText(stmt.raw, 1, pp_id);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    *out = readUser(stmt.raw, 0);
    out->salt = columnText(stmt.raw, 6);
    out->password_hash = columnText(stmt.raw, 7);
    return true;
}

std::vector<UserRow> Database::allUsers() const {
    std::vector<UserRow> users;
    if (db_ == nullptr) return users;

    Stmt stmt;
    if (!prepare(db_, "SELECT uid, username, nickname, pp_id, signature, avatar_url FROM users ORDER BY uid ASC", &stmt)) {
        return users;
    }
    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        users.push_back(readUser(stmt.raw, 0));
    }
    return users;
}

std::vector<UserRow> Database::searchUsers(const std::string& keyword,
                                           std::uint64_t exclude_uid,
                                           std::uint32_t limit) const {
    std::vector<UserRow> users;
    if (db_ == nullptr) return users;
    if (limit == 0) limit = 20;
    if (limit > 50) limit = 50;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT uid, username, nickname, pp_id, signature, avatar_url FROM users "
                 "WHERE uid <> ?1 AND (pp_id = ?2 COLLATE NOCASE "
                 "                     OR username = ?2 COLLATE NOCASE "
                 "                     OR nickname = ?2) "
                 "LIMIT ?3",
                 &stmt)) {
        return users;
    }

    bindU64(stmt.raw, 1, exclude_uid);
    bindText(stmt.raw, 2, keyword);
    sqlite3_bind_int(stmt.raw, 3, static_cast<int>(limit));

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        users.push_back(readUser(stmt.raw, 0));
    }
    return users;
}

bool Database::updateProfile(std::uint64_t uid,
                             const std::string& nickname,
                             const std::string& signature,
                             const std::string& pp_id,
                             std::string* err,
                             const std::string& avatar_url) {
    if (db_ == nullptr) {
        if (err) *err = "数据库未打开";
        return false;
    }

    if (!pp_id.empty()) {
        UserRow other;
        if (findUserByPpId(pp_id, &other) && other.uid != uid) {
            if (err) *err = "该 pp 号已被占用";
            return false;
        }
    }

    Stmt stmt;
    if (!prepare(db_, "UPDATE users SET nickname = ?2, signature = ?3, "
                       "pp_id = CASE WHEN ?4 = '' THEN pp_id ELSE ?4 END, "
                       "avatar_url = CASE WHEN ?5 = '' THEN avatar_url ELSE ?5 END "
                       "WHERE uid = ?1",
                 &stmt)) {
        if (err) *err = "数据库错误";
        return false;
    }

    bindU64(stmt.raw, 1, uid);
    bindText(stmt.raw, 2, nickname);
    bindText(stmt.raw, 3, signature);
    bindText(stmt.raw, 4, pp_id);
    bindText(stmt.raw, 5, avatar_url);

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        if (err) *err = (sqlite3_extended_errcode(db_) == SQLITE_CONSTRAINT_UNIQUE) ? "该 pp 号已被占用" : "数据库写入失败";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  好友
// ---------------------------------------------------------------------------
std::vector<UserRow> Database::friendsOf(std::uint64_t uid) const {
    std::vector<UserRow> users;
    if (db_ == nullptr) return users;

    Stmt stmt;
    if (!prepare(db_, "SELECT u.uid, u.username, u.nickname, u.pp_id, u.signature, u.avatar_url "
                       "FROM friendships f JOIN users u ON u.uid = f.peer_uid "
                       "WHERE f.uid = ? ORDER BY u.nickname ASC", &stmt)) {
        return users;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        users.push_back(readUser(stmt.raw, 0));
    }
    return users;
}

bool Database::areFriends(std::uint64_t a, std::uint64_t b) const {
    if (db_ == nullptr || a == 0 || b == 0 || a == b) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT 1 FROM friendships WHERE uid = ? AND peer_uid = ?", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, a);
    bindU64(stmt.raw, 2, b);
    return sqlite3_step(stmt.raw) == SQLITE_ROW;
}

void Database::addFriendPair(std::uint64_t a, std::uint64_t b) {
    if (db_ == nullptr || a == 0 || b == 0 || a == b) return;

    Stmt stmt;
    if (!prepare(db_, "INSERT OR IGNORE INTO friendships(uid, peer_uid, created_at) VALUES(?1, ?2, strftime('%s','now'))", &stmt)) {
        return;
    }
    bindU64(stmt.raw, 1, a);
    bindU64(stmt.raw, 2, b);
    sqlite3_step(stmt.raw);

    Stmt reverse;
    if (prepare(db_, "INSERT OR IGNORE INTO friendships(uid, peer_uid, created_at) VALUES(?1, ?2, strftime('%s','now'))", &reverse)) {
        bindU64(reverse.raw, 1, b);
        bindU64(reverse.raw, 2, a);
        sqlite3_step(reverse.raw);
    }
}

bool Database::hasPendingRequest(std::uint64_t from_uid, std::uint64_t to_uid) const {
    if (db_ == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT 1 FROM friend_requests WHERE from_uid = ? AND to_uid = ? AND status = 0", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, from_uid);
    bindU64(stmt.raw, 2, to_uid);
    return sqlite3_step(stmt.raw) == SQLITE_ROW;
}

bool Database::findPendingRequestBetween(std::uint64_t a, std::uint64_t b, std::uint64_t* request_id) const {
    if (db_ == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT id FROM friend_requests WHERE status = 0 AND "
                 "((from_uid = ?1 AND to_uid = ?2) OR (from_uid = ?2 AND to_uid = ?1)) "
                 "ORDER BY id ASC LIMIT 1",
                 &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, a);
    bindU64(stmt.raw, 2, b);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;
    if (request_id != nullptr) {
        *request_id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
    }
    return true;
}

std::uint64_t Database::createFriendRequest(std::uint64_t from_uid,
                                            std::uint64_t to_uid,
                                            const std::string& message,
                                            std::int64_t created_at) {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO friend_requests(from_uid, to_uid, message, status, created_at) "
                       "VALUES(?,?,?,0,?)", &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, from_uid);
    bindU64(stmt.raw, 2, to_uid);
    bindText(stmt.raw, 3, message);
    sqlite3_bind_int64(stmt.raw, 4, static_cast<sqlite3_int64>(created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] createFriendRequest failed: {}", sqlite3_errmsg(db_));
        return 0;
    }
    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
}

std::vector<FriendRequestRow> Database::pendingRequestsFor(std::uint64_t uid) const {
    std::vector<FriendRequestRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_, "SELECT id, from_uid, to_uid, message, status, created_at "
                       "FROM friend_requests WHERE to_uid = ? AND status = 0 "
                       "ORDER BY id DESC LIMIT 100", &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        FriendRequestRow row;
        row.id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.from_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
        row.to_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
        row.message = columnText(stmt.raw, 3);
        row.status = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 4));
        row.created_at = sqlite3_column_int64(stmt.raw, 5);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Database::getFriendRequest(std::uint64_t request_id, FriendRequestRow* out) const {
    if (db_ == nullptr || out == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT id, from_uid, to_uid, message, status, created_at "
                       "FROM friend_requests WHERE id = ?", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, request_id);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    out->id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
    out->from_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
    out->to_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
    out->message = columnText(stmt.raw, 3);
    out->status = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 4));
    out->created_at = sqlite3_column_int64(stmt.raw, 5);
    return true;
}

void Database::setFriendRequestStatus(std::uint64_t request_id, std::uint32_t status) {
    if (db_ == nullptr) return;

    Stmt stmt;
    if (!prepare(db_, "UPDATE friend_requests SET status = ? WHERE id = ?", &stmt)) {
        return;
    }
    sqlite3_bind_int(stmt.raw, 1, static_cast<int>(status));
    bindU64(stmt.raw, 2, request_id);
    sqlite3_step(stmt.raw);
}

// ---------------------------------------------------------------------------
//  私聊
// ---------------------------------------------------------------------------
std::uint64_t Database::insertMessage(std::uint64_t from_uid,
                                      std::uint64_t to_uid,
                                      const std::string& content,
                                      std::int64_t created_at,
                                      std::uint32_t msg_kind,
                                      const std::string& file_url,
                                      const std::string& file_name,
                                      std::uint64_t file_size,
                                      const std::string& quote_id,
                                      const std::string& quote_text,
                                      const std::string& quote_nickname) {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO messages(from_uid, to_uid, group_id, content, kind, msg_kind, "
                       "file_url, file_name, file_size, quote_id, quote_text, quote_nickname, created_at) "
                       "VALUES(?,?,0,?,1,?,?,?,?,?,?,?,?)",
                 &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, from_uid);
    bindU64(stmt.raw, 2, to_uid);
    bindText(stmt.raw, 3, content);
    sqlite3_bind_int(stmt.raw, 4, static_cast<int>(msg_kind));
    bindText(stmt.raw, 5, file_url);
    bindText(stmt.raw, 6, file_name);
    sqlite3_bind_int64(stmt.raw, 7, static_cast<sqlite3_int64>(file_size));
    bindText(stmt.raw, 8, quote_id);
    bindText(stmt.raw, 9, quote_text);
    bindText(stmt.raw, 10, quote_nickname);
    sqlite3_bind_int64(stmt.raw, 11, static_cast<sqlite3_int64>(created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] insert message failed: {}", sqlite3_errmsg(db_));
        return 0;
    }
    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
}

std::vector<MessageRow> Database::history(std::uint64_t uid,
                                          std::uint64_t peer_uid,
                                          std::uint64_t before_id,
                                          std::uint32_t limit,
                                          bool* has_more) const {
    std::vector<MessageRow> rows;
    if (has_more) *has_more = false;
    if (db_ == nullptr) return rows;

    if (limit == 0) limit = 30;
    if (limit > 100) limit = 100;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT id, from_uid, to_uid, content, created_at, msg_kind, file_url, file_name, file_size, "
                 "quote_id, quote_text, quote_nickname, recalled "
                 "FROM messages "
                 "WHERE group_id = 0 "
                 "  AND ((from_uid = ?1 AND to_uid = ?2) OR (from_uid = ?2 AND to_uid = ?1)) "
                 "  AND (?3 = 0 OR id < ?3) "
                 "ORDER BY id DESC LIMIT ?4",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);
    bindU64(stmt.raw, 2, peer_uid);
    bindU64(stmt.raw, 3, before_id);
    sqlite3_bind_int64(stmt.raw, 4, static_cast<sqlite3_int64>(limit) + 1);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        MessageRow row;
        row.id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.from_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
        row.to_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
        row.content = columnText(stmt.raw, 3);
        row.created_at = sqlite3_column_int64(stmt.raw, 4);
        row.msg_kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 5));
        row.file_url = columnText(stmt.raw, 6);
        row.file_name = columnText(stmt.raw, 7);
        row.file_size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 8));
        row.quote_id = columnText(stmt.raw, 9);
        row.quote_text = columnText(stmt.raw, 10);
        row.quote_nickname = columnText(stmt.raw, 11);
        row.recalled = sqlite3_column_int(stmt.raw, 12) != 0;
        rows.push_back(std::move(row));
    }

    if (rows.size() > limit) {
        if (has_more) *has_more = true;
        rows.pop_back();
    }
    std::reverse(rows.begin(), rows.end());
    return rows;
}

bool Database::getMessage(std::uint64_t msg_id, MessageRow* out) const {
    if (db_ == nullptr || out == nullptr || msg_id == 0) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT id, from_uid, to_uid, group_id, content, created_at, msg_kind, "
                 "file_url, file_name, file_size, quote_id, quote_text, quote_nickname, recalled "
                 "FROM messages WHERE id = ?1",
                 &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, msg_id);
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    out->id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
    out->from_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
    out->to_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
    out->group_id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 3));
    out->content = columnText(stmt.raw, 4);
    out->created_at = sqlite3_column_int64(stmt.raw, 5);
    out->msg_kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 6));
    out->file_url = columnText(stmt.raw, 7);
    out->file_name = columnText(stmt.raw, 8);
    out->file_size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 9));
    out->quote_id = columnText(stmt.raw, 10);
    out->quote_text = columnText(stmt.raw, 11);
    out->quote_nickname = columnText(stmt.raw, 12);
    out->recalled = sqlite3_column_int(stmt.raw, 13) != 0;
    return true;
}

bool Database::recallMessage(std::uint64_t msg_id, std::uint64_t from_uid) {
    if (db_ == nullptr || msg_id == 0) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "UPDATE messages SET recalled = 1, content = '[消息已撤回]', "
                 "file_url = '', file_name = '', file_size = 0, "
                 "quote_id = '', quote_text = '', quote_nickname = '' "
                 "WHERE id = ?1 AND from_uid = ?2 AND recalled = 0",
                 &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, msg_id);
    bindU64(stmt.raw, 2, from_uid);
    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] recall message failed: {}", sqlite3_errmsg(db_));
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

std::vector<ConversationRow> Database::privateConversations(std::uint64_t uid) const {
    std::vector<ConversationRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_,
                 "WITH pairs AS ("
                 "  SELECT CASE WHEN from_uid = ?1 THEN to_uid ELSE from_uid END AS peer_uid,"
                 "         MAX(id) AS last_id"
                 "  FROM messages WHERE group_id = 0 AND (from_uid = ?1 OR to_uid = ?1)"
                 "  GROUP BY peer_uid"
                 ") "
                 "SELECT p.peer_uid, "
                 "       CASE WHEN m.msg_kind = 4 THEN '[拍一拍]' ELSE m.content END, "
                 "       m.created_at, u.nickname "
                 "FROM pairs p "
                 "JOIN messages m ON m.id = p.last_id "
                 "JOIN users    u ON u.uid = p.peer_uid "
                 "ORDER BY p.last_id DESC LIMIT 200",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        ConversationRow row;
        row.peer_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.last_content = columnText(stmt.raw, 1);
        row.last_time = sqlite3_column_int64(stmt.raw, 2);
        row.nickname = columnText(stmt.raw, 3);
        row.unread = unreadCount(uid, row.peer_uid);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint32_t Database::unreadCount(std::uint64_t uid, std::uint64_t peer_uid) const {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT COUNT(*) FROM messages "
                 "WHERE group_id = 0 AND from_uid = ?1 AND to_uid = ?2 "
                 "  AND id > COALESCE((SELECT last_read_id FROM read_state WHERE uid = ?2 AND peer_uid = ?1), 0)",
                 &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, peer_uid);
    bindU64(stmt.raw, 2, uid);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return 0;
    return static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
}

void Database::markRead(std::uint64_t uid, std::uint64_t peer_uid) {
    if (db_ == nullptr) return;

    Stmt stmt;
    if (!prepare(db_,
                 "INSERT INTO read_state(uid, peer_uid, last_read_id) "
                 "VALUES(?1, ?2, COALESCE((SELECT MAX(id) FROM messages WHERE group_id = 0 "
                 "       AND ((from_uid = ?1 AND to_uid = ?2) OR (from_uid = ?2 AND to_uid = ?1))), 0)) "
                 "ON CONFLICT(uid, peer_uid) DO UPDATE SET last_read_id = MAX(last_read_id, excluded.last_read_id)",
                 &stmt)) {
        return;
    }
    bindU64(stmt.raw, 1, uid);
    bindU64(stmt.raw, 2, peer_uid);
    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] markRead failed: {}", sqlite3_errmsg(db_));
    }
}

// ---------------------------------------------------------------------------
//  群聊
// ---------------------------------------------------------------------------
std::uint64_t Database::createGroup(const std::string& name,
                                    std::uint64_t owner_uid,
                                    const std::vector<std::uint64_t>& members,
                                    std::int64_t created_at) {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO chat_groups(name, owner_uid, created_at) VALUES(?,?,?)", &stmt)) {
        return 0;
    }
    bindText(stmt.raw, 1, name);
    bindU64(stmt.raw, 2, owner_uid);
    sqlite3_bind_int64(stmt.raw, 3, static_cast<sqlite3_int64>(created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] createGroup failed: {}", sqlite3_errmsg(db_));
        return 0;
    }

    const std::uint64_t gid = static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
    addGroupMember(gid, owner_uid, 1, created_at);
    for (std::uint64_t uid : members) {
        if (uid != owner_uid) {
            addGroupMember(gid, uid, 0, created_at);
        }
    }
    return gid;
}

bool Database::groupExists(std::uint64_t gid) const {
    if (db_ == nullptr || gid == 0) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT 1 FROM chat_groups WHERE gid = ?", &stmt)) return false;
    bindU64(stmt.raw, 1, gid);
    return sqlite3_step(stmt.raw) == SQLITE_ROW;
}

bool Database::getGroup(std::uint64_t gid, GroupRow* out) const {
    if (db_ == nullptr || out == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT g.gid, g.name, g.owner_uid, g.created_at, "
                 "  (SELECT COUNT(*) FROM group_members gm WHERE gm.gid = g.gid) "
                 "FROM chat_groups g WHERE g.gid = ?",
                 &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, gid);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    out->gid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
    out->name = columnText(stmt.raw, 1);
    out->owner_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
    out->created_at = sqlite3_column_int64(stmt.raw, 3);
    out->member_count = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 4));
    return true;
}

std::vector<GroupRow> Database::groupsOf(std::uint64_t uid) const {
    std::vector<GroupRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT g.gid, g.name, g.owner_uid, g.created_at, "
                 "  (SELECT COUNT(*) FROM group_members gm2 WHERE gm2.gid = g.gid) "
                 "FROM chat_groups g JOIN group_members gm ON gm.gid = g.gid "
                 "WHERE gm.uid = ? ORDER BY g.gid DESC",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        GroupRow row;
        row.gid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.name = columnText(stmt.raw, 1);
        row.owner_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
        row.created_at = sqlite3_column_int64(stmt.raw, 3);
        row.member_count = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 4));
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<GroupMemberRow> Database::groupMembers(std::uint64_t gid) const {
    std::vector<GroupMemberRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT gm.gid, gm.uid, gm.role, gm.joined_at, u.nickname, u.pp_id "
                 "FROM group_members gm JOIN users u ON u.uid = gm.uid "
                 "WHERE gm.gid = ? ORDER BY gm.role DESC, gm.joined_at ASC",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, gid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        GroupMemberRow row;
        row.gid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
        row.role = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 2));
        row.joined_at = sqlite3_column_int64(stmt.raw, 3);
        row.nickname = columnText(stmt.raw, 4);
        row.pp_id = columnText(stmt.raw, 5);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Database::isGroupMember(std::uint64_t gid, std::uint64_t uid) const {
    if (db_ == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT 1 FROM group_members WHERE gid = ? AND uid = ?", &stmt)) return false;
    bindU64(stmt.raw, 1, gid);
    bindU64(stmt.raw, 2, uid);
    return sqlite3_step(stmt.raw) == SQLITE_ROW;
}

std::uint32_t Database::groupMemberCount(std::uint64_t gid) const {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "SELECT COUNT(*) FROM group_members WHERE gid = ?", &stmt)) return 0;
    bindU64(stmt.raw, 1, gid);
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return 0;
    return static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
}

void Database::addGroupMember(std::uint64_t gid, std::uint64_t uid, std::uint32_t role, std::int64_t joined_at) {
    if (db_ == nullptr) return;

    Stmt stmt;
    if (!prepare(db_, "INSERT OR IGNORE INTO group_members(gid, uid, role, joined_at) VALUES(?,?,?,?)", &stmt)) {
        return;
    }
    bindU64(stmt.raw, 1, gid);
    bindU64(stmt.raw, 2, uid);
    sqlite3_bind_int(stmt.raw, 3, static_cast<int>(role));
    sqlite3_bind_int64(stmt.raw, 4, static_cast<sqlite3_int64>(joined_at));
    sqlite3_step(stmt.raw);
}

void Database::removeGroupMember(std::uint64_t gid, std::uint64_t uid) {
    if (db_ == nullptr) return;

    Stmt stmt;
    if (!prepare(db_, "DELETE FROM group_members WHERE gid = ? AND uid = ?", &stmt)) return;
    bindU64(stmt.raw, 1, gid);
    bindU64(stmt.raw, 2, uid);
    sqlite3_step(stmt.raw);
}

void Database::deleteGroup(std::uint64_t gid) {
    if (db_ == nullptr) return;

    exec("BEGIN");
    Stmt m1, m2, m3, m4;
    if (prepare(db_, "DELETE FROM group_members WHERE gid = ?", &m1)) {
        bindU64(m1.raw, 1, gid);
        sqlite3_step(m1.raw);
    }
    if (prepare(db_, "DELETE FROM group_read_state WHERE gid = ?", &m2)) {
        bindU64(m2.raw, 1, gid);
        sqlite3_step(m2.raw);
    }
    if (prepare(db_, "DELETE FROM messages WHERE group_id = ?", &m3)) {
        bindU64(m3.raw, 1, gid);
        sqlite3_step(m3.raw);
    }
    if (prepare(db_, "DELETE FROM chat_groups WHERE gid = ?", &m4)) {
        bindU64(m4.raw, 1, gid);
        sqlite3_step(m4.raw);
    }
    exec("COMMIT");
}

std::uint64_t Database::insertGroupMessage(std::uint64_t gid,
                                           std::uint64_t from_uid,
                                           const std::string& content,
                                           std::uint32_t kind,
                                           std::int64_t created_at,
                                           std::uint32_t msg_kind,
                                           const std::string& file_url,
                                           const std::string& file_name,
                                           std::uint64_t file_size,
                                           const std::string& quote_id,
                                           const std::string& quote_text,
                                           const std::string& quote_nickname) {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO messages(from_uid, to_uid, group_id, content, kind, msg_kind, "
                       "file_url, file_name, file_size, quote_id, quote_text, quote_nickname, created_at) "
                       "VALUES(?,0,?,?,?,?,?,?,?,?,?,?,?)",
                 &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, from_uid);
    bindU64(stmt.raw, 2, gid);
    bindText(stmt.raw, 3, content);
    sqlite3_bind_int(stmt.raw, 4, static_cast<int>(kind));
    sqlite3_bind_int(stmt.raw, 5, static_cast<int>(msg_kind));
    bindText(stmt.raw, 6, file_url);
    bindText(stmt.raw, 7, file_name);
    sqlite3_bind_int64(stmt.raw, 8, static_cast<sqlite3_int64>(file_size));
    bindText(stmt.raw, 9, quote_id);
    bindText(stmt.raw, 10, quote_text);
    bindText(stmt.raw, 11, quote_nickname);
    sqlite3_bind_int64(stmt.raw, 12, static_cast<sqlite3_int64>(created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] insertGroupMessage failed: {}", sqlite3_errmsg(db_));
        return 0;
    }
    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
}

std::vector<MessageRow> Database::groupHistory(std::uint64_t gid,
                                               std::uint64_t before_id,
                                               std::uint32_t limit,
                                               bool* has_more) const {
    std::vector<MessageRow> rows;
    if (has_more) *has_more = false;
    if (db_ == nullptr) return rows;

    if (limit == 0) limit = 30;
    if (limit > 100) limit = 100;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT id, from_uid, content, kind, created_at, msg_kind, file_url, file_name, file_size, "
                 "quote_id, quote_text, quote_nickname, recalled "
                 "FROM messages "
                 "WHERE group_id = ?1 AND (?2 = 0 OR id < ?2) "
                 "ORDER BY id DESC LIMIT ?3",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, gid);
    bindU64(stmt.raw, 2, before_id);
    sqlite3_bind_int64(stmt.raw, 3, static_cast<sqlite3_int64>(limit) + 1);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        MessageRow row;
        row.id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.from_uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
        row.group_id = gid;
        row.content = columnText(stmt.raw, 2);
        row.kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 3));
        row.created_at = sqlite3_column_int64(stmt.raw, 4);
        row.msg_kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 5));
        row.file_url = columnText(stmt.raw, 6);
        row.file_name = columnText(stmt.raw, 7);
        row.file_size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 8));
        row.quote_id = columnText(stmt.raw, 9);
        row.quote_text = columnText(stmt.raw, 10);
        row.quote_nickname = columnText(stmt.raw, 11);
        row.recalled = sqlite3_column_int(stmt.raw, 12) != 0;
        rows.push_back(std::move(row));
    }

    if (rows.size() > limit) {
        if (has_more) *has_more = true;
        rows.pop_back();
    }
    std::reverse(rows.begin(), rows.end());
    return rows;
}

std::vector<GroupConversationRow> Database::groupConversations(std::uint64_t uid) const {
    std::vector<GroupConversationRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_,
                 "WITH last AS ("
                 "  SELECT group_id, MAX(id) AS last_id FROM messages "
                 "  WHERE group_id <> 0 AND group_id IN (SELECT gid FROM group_members WHERE uid = ?1) "
                 "  GROUP BY group_id"
                 ") "
                 "SELECT l.group_id, g.name, "
                 "       CASE WHEN m.msg_kind = 4 THEN '[拍一拍]' ELSE m.content END, "
                 "       m.created_at, COALESCE(u.nickname,''), m.kind "
                 "FROM last l "
                 "JOIN chat_groups g ON g.gid = l.group_id "
                 "JOIN messages m ON m.id = l.last_id "
                 "LEFT JOIN users u ON u.uid = m.from_uid "
                 "ORDER BY l.last_id DESC LIMIT 200",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        GroupConversationRow row;
        row.gid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.name = columnText(stmt.raw, 1);
        row.last_content = columnText(stmt.raw, 2);
        row.last_time = sqlite3_column_int64(stmt.raw, 3);
        row.last_sender = columnText(stmt.raw, 4);
        const std::uint32_t kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 5));
        if (kind == 2) {
            row.last_sender.clear();
        }
        row.unread = groupUnread(uid, row.gid);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint32_t Database::groupUnread(std::uint64_t uid, std::uint64_t gid) const {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT COUNT(*) FROM messages "
                 "WHERE group_id = ?2 AND from_uid <> ?1 AND kind = 1 "
                 "  AND id > COALESCE((SELECT last_read_id FROM group_read_state WHERE uid = ?1 AND gid = ?2), 0)",
                 &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, uid);
    bindU64(stmt.raw, 2, gid);

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return 0;
    return static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
}

void Database::markGroupRead(std::uint64_t uid, std::uint64_t gid) {
    if (db_ == nullptr) return;

    Stmt stmt;
    if (!prepare(db_,
                 "INSERT INTO group_read_state(uid, gid, last_read_id) "
                 "VALUES(?1, ?2, COALESCE((SELECT MAX(id) FROM messages WHERE group_id = ?2), 0)) "
                 "ON CONFLICT(uid, gid) DO UPDATE SET last_read_id = MAX(last_read_id, excluded.last_read_id)",
                 &stmt)) {
        return;
    }
    bindU64(stmt.raw, 1, uid);
    bindU64(stmt.raw, 2, gid);
    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] markGroupRead failed: {}", sqlite3_errmsg(db_));
    }
}

// ---------------------------------------------------------------------------
//  动态（朋友圈 / 微博）
// ---------------------------------------------------------------------------
namespace {

std::string joinImages(const std::vector<std::string>& images) {
    std::string out;
    for (std::size_t i = 0; i < images.size(); ++i) {
        if (i != 0) out.push_back('\n');
        out += images[i];
    }
    return out;
}

std::vector<std::string> splitImages(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find('\n', start);
        const std::string part = text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!part.empty()) out.push_back(part);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

PostRow readPost(sqlite3_stmt* stmt) {
    PostRow row;
    row.id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
    row.uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
    row.content = columnText(stmt, 2);
    row.images = splitImages(columnText(stmt, 3));
    row.created_at = sqlite3_column_int64(stmt, 4);
    return row;
}

}  // namespace

std::uint64_t Database::createPost(std::uint64_t uid,
                                   const std::string& content,
                                   const std::vector<std::string>& images,
                                   std::int64_t created_at) {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO posts(uid, content, images, deleted, created_at) VALUES(?,?,?,0,?)", &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, uid);
    bindText(stmt.raw, 2, content);
    bindText(stmt.raw, 3, joinImages(images));
    sqlite3_bind_int64(stmt.raw, 4, static_cast<sqlite3_int64>(created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] createPost failed: {}", sqlite3_errmsg(db_));
        return 0;
    }
    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
}

bool Database::getPost(std::uint64_t post_id, PostRow* out) const {
    if (db_ == nullptr || out == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT id, uid, content, images, created_at FROM posts WHERE id = ? AND deleted = 0", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, post_id);
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    *out = readPost(stmt.raw);
    return true;
}

std::vector<PostRow> Database::feed(std::uint64_t viewer_uid,
                                    std::uint64_t author_uid,
                                    std::uint64_t before_id,
                                    std::uint32_t limit,
                                    bool* has_more) const {
    std::vector<PostRow> rows;
    if (has_more) *has_more = false;
    if (db_ == nullptr) return rows;

    if (limit == 0) limit = 20;
    if (limit > 50) limit = 50;

    Stmt stmt;
    if (author_uid == 0) {
        if (!prepare(db_,
                     "SELECT id, uid, content, images, created_at FROM posts "
                     "WHERE deleted = 0 AND (?3 = 0 OR id < ?3) "
                     "  AND (uid = ?1 OR uid IN (SELECT peer_uid FROM friendships WHERE uid = ?1)) "
                     "ORDER BY id DESC LIMIT ?4",
                     &stmt)) {
            return rows;
        }
        bindU64(stmt.raw, 1, viewer_uid);
    } else {
        if (!prepare(db_,
                     "SELECT id, uid, content, images, created_at FROM posts "
                     "WHERE deleted = 0 AND uid = ?1 AND (?3 = 0 OR id < ?3) "
                     "ORDER BY id DESC LIMIT ?4",
                     &stmt)) {
            return rows;
        }
        bindU64(stmt.raw, 1, author_uid);
    }
    bindU64(stmt.raw, 3, before_id);
    sqlite3_bind_int64(stmt.raw, 4, static_cast<sqlite3_int64>(limit) + 1);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        rows.push_back(readPost(stmt.raw));
    }

    if (rows.size() > limit) {
        if (has_more) *has_more = true;
        rows.pop_back();
    }
    return rows;
}

bool Database::deletePost(std::uint64_t post_id, std::uint64_t uid) {
    if (db_ == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "UPDATE posts SET deleted = 1 WHERE id = ? AND uid = ?", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, post_id);
    bindU64(stmt.raw, 2, uid);
    if (sqlite3_step(stmt.raw) != SQLITE_DONE) return false;
    return sqlite3_changes(db_) > 0;
}

bool Database::canViewPost(std::uint64_t post_id, std::uint64_t uid) const {
    PostRow post;
    if (!getPost(post_id, &post)) return false;
    if (post.uid == uid) return true;
    return areFriends(uid, post.uid);
}

bool Database::setPostLike(std::uint64_t post_id, std::uint64_t uid, bool like) {
    if (db_ == nullptr) return false;

    if (like) {
        Stmt stmt;
        if (!prepare(db_, "INSERT OR IGNORE INTO post_likes(post_id, uid, created_at) "
                          "VALUES(?,?,strftime('%s','now'))", &stmt)) {
            return false;
        }
        bindU64(stmt.raw, 1, post_id);
        bindU64(stmt.raw, 2, uid);
        return sqlite3_step(stmt.raw) == SQLITE_DONE;
    }

    Stmt stmt;
    if (!prepare(db_, "DELETE FROM post_likes WHERE post_id = ? AND uid = ?", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, post_id);
    bindU64(stmt.raw, 2, uid);
    return sqlite3_step(stmt.raw) == SQLITE_DONE;
}

std::uint32_t Database::likeCount(std::uint64_t post_id) const {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "SELECT COUNT(*) FROM post_likes WHERE post_id = ?", &stmt)) return 0;
    bindU64(stmt.raw, 1, post_id);
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return 0;
    return static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
}

bool Database::hasLiked(std::uint64_t post_id, std::uint64_t uid) const {
    if (db_ == nullptr) return false;

    Stmt stmt;
    if (!prepare(db_, "SELECT 1 FROM post_likes WHERE post_id = ? AND uid = ?", &stmt)) return false;
    bindU64(stmt.raw, 1, post_id);
    bindU64(stmt.raw, 2, uid);
    return sqlite3_step(stmt.raw) == SQLITE_ROW;
}

std::uint64_t Database::addComment(std::uint64_t post_id,
                                   std::uint64_t uid,
                                   const std::string& content,
                                   std::int64_t created_at) {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "INSERT INTO post_comments(post_id, uid, content, created_at) VALUES(?,?,?,?)", &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, post_id);
    bindU64(stmt.raw, 2, uid);
    bindText(stmt.raw, 3, content);
    sqlite3_bind_int64(stmt.raw, 4, static_cast<sqlite3_int64>(created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] addComment failed: {}", sqlite3_errmsg(db_));
        return 0;
    }
    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
}

std::vector<PostCommentRow> Database::commentsOf(std::uint64_t post_id, std::uint32_t limit) const {
    std::vector<PostCommentRow> rows;
    if (db_ == nullptr) return rows;
    if (limit == 0) limit = 30;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT c.id, c.post_id, c.uid, c.content, c.created_at, u.nickname "
                 "FROM post_comments c LEFT JOIN users u ON u.uid = c.uid "
                 "WHERE c.post_id = ? ORDER BY c.id DESC LIMIT ?",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, post_id);
    sqlite3_bind_int64(stmt.raw, 2, static_cast<sqlite3_int64>(limit));

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        PostCommentRow row;
        row.id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 0));
        row.post_id = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
        row.uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 2));
        row.content = columnText(stmt.raw, 3);
        row.created_at = sqlite3_column_int64(stmt.raw, 4);
        row.nickname = columnText(stmt.raw, 5);
        if (row.nickname.empty()) row.nickname = "用户 " + std::to_string(row.uid);
        rows.push_back(std::move(row));
    }
    std::reverse(rows.begin(), rows.end());
    return rows;
}

std::uint32_t Database::commentCount(std::uint64_t post_id) const {
    if (db_ == nullptr) return 0;

    Stmt stmt;
    if (!prepare(db_, "SELECT COUNT(*) FROM post_comments WHERE post_id = ?", &stmt)) return 0;
    bindU64(stmt.raw, 1, post_id);
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return 0;
    return static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
}

// ---------------------------------------------------------------------------
//  OAuth / 会话令牌
// ---------------------------------------------------------------------------
std::string Database::createAuthToken(std::uint32_t kind,
                                      std::uint64_t uid,
                                      const std::string& client_id,
                                      const std::string& redirect_uri,
                                      std::int64_t expires_at,
                                      const std::string& scope) {
    if (db_ == nullptr) return {};

    const std::string token = im::crypto::randomHex(32);
    const std::string hash = im::crypto::sha256Hex(token);

    Stmt stmt;
    if (!prepare(db_,
                 "INSERT INTO auth_tokens(token_hash, kind, uid, client_id, redirect_uri, scope, expires_at, used, created_at) "
                 "VALUES(?, ?, ?, ?, ?, ?, ?, 0, strftime('%s','now'))",
                 &stmt)) {
        return {};
    }
    bindText(stmt.raw, 1, hash);
    sqlite3_bind_int(stmt.raw, 2, static_cast<int>(kind));
    bindU64(stmt.raw, 3, uid);
    bindText(stmt.raw, 4, client_id);
    bindText(stmt.raw, 5, redirect_uri);
    bindText(stmt.raw, 6, scope.empty() ? std::string("profile") : scope);
    sqlite3_bind_int64(stmt.raw, 7, static_cast<sqlite3_int64>(expires_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] createAuthToken failed: {}", sqlite3_errmsg(db_));
        return {};
    }
    return token;
}

bool Database::findAuthToken(const std::string& token, std::uint32_t kind, AuthTokenRow* out) const {
    if (db_ == nullptr || out == nullptr || token.empty()) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT kind, uid, client_id, redirect_uri, expires_at FROM auth_tokens "
                 "WHERE token_hash = ? AND kind = ? AND used = 0 AND expires_at > strftime('%s','now')",
                 &stmt)) {
        return false;
    }
    bindText(stmt.raw, 1, im::crypto::sha256Hex(token));
    sqlite3_bind_int(stmt.raw, 2, static_cast<int>(kind));

    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    out->kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
    out->uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
    out->client_id = columnText(stmt.raw, 2);
    out->redirect_uri = columnText(stmt.raw, 3);
    out->expires_at = sqlite3_column_int64(stmt.raw, 4);
    return true;
}

bool Database::consumeAuthToken(const std::string& token, std::uint32_t kind, AuthTokenRow* out) {
    if (db_ == nullptr || out == nullptr || token.empty()) return false;

    // 先原子地把 used 从 0 改成 1，只有改成功的那一个请求才能继续读内容，
    // 这样两个人同时拿同一个 code 来换 token 时，只有一个能成功。
    Stmt update;
    if (!prepare(db_,
                 "UPDATE auth_tokens SET used = 1 "
                 "WHERE token_hash = ? AND kind = ? AND used = 0 AND expires_at > strftime('%s','now')",
                 &update)) {
        return false;
    }
    bindText(update.raw, 1, im::crypto::sha256Hex(token));
    sqlite3_bind_int(update.raw, 2, static_cast<int>(kind));
    if (sqlite3_step(update.raw) != SQLITE_DONE) return false;
    if (sqlite3_changes(db_) != 1) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT kind, uid, client_id, redirect_uri, expires_at FROM auth_tokens WHERE token_hash = ?",
                 &stmt)) {
        return false;
    }
    bindText(stmt.raw, 1, im::crypto::sha256Hex(token));
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    out->kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 0));
    out->uid = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 1));
    out->client_id = columnText(stmt.raw, 2);
    out->redirect_uri = columnText(stmt.raw, 3);
    out->expires_at = sqlite3_column_int64(stmt.raw, 4);
    return true;
}

void Database::deleteAuthToken(const std::string& token) {
    if (db_ == nullptr || token.empty()) return;

    Stmt stmt;
    if (!prepare(db_, "DELETE FROM auth_tokens WHERE token_hash = ?", &stmt)) return;
    bindText(stmt.raw, 1, im::crypto::sha256Hex(token));
    sqlite3_step(stmt.raw);
}

void Database::purgeExpiredAuthTokens(std::int64_t now) {
    if (db_ == nullptr) return;

    Stmt stmt;
    if (!prepare(db_, "DELETE FROM auth_tokens WHERE expires_at <= ?", &stmt)) return;
    sqlite3_bind_int64(stmt.raw, 1, static_cast<sqlite3_int64>(now));
    sqlite3_step(stmt.raw);
}

bool Database::upsertOauthClient(const OauthClientRow& row) {
    if (db_ == nullptr || row.client_id.empty()) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "INSERT INTO oauth_clients(client_id, client_secret_hash, name, redirect_uris, created_at) "
                 "VALUES(?, ?, ?, ?, strftime('%s','now')) "
                 "ON CONFLICT(client_id) DO UPDATE SET "
                 "  client_secret_hash = excluded.client_secret_hash, "
                 "  name = excluded.name, "
                 "  redirect_uris = excluded.redirect_uris",
                 &stmt)) {
        return false;
    }
    bindText(stmt.raw, 1, row.client_id);
    bindText(stmt.raw, 2, row.client_secret_hash);
    bindText(stmt.raw, 3, row.name);
    bindText(stmt.raw, 4, row.redirect_uris);
    return sqlite3_step(stmt.raw) == SQLITE_DONE;
}

bool Database::getOauthClient(const std::string& client_id, OauthClientRow* out) const {
    if (db_ == nullptr || out == nullptr || client_id.empty()) return false;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT client_id, client_secret_hash, name, redirect_uris, created_at "
                 "FROM oauth_clients WHERE client_id = ?",
                 &stmt)) {
        return false;
    }
    bindText(stmt.raw, 1, client_id);
    if (sqlite3_step(stmt.raw) != SQLITE_ROW) return false;

    out->client_id = columnText(stmt.raw, 0);
    out->client_secret_hash = columnText(stmt.raw, 1);
    out->name = columnText(stmt.raw, 2);
    out->redirect_uris = columnText(stmt.raw, 3);
    out->created_at = sqlite3_column_int64(stmt.raw, 4);
    return true;
}

std::vector<OauthGrantRow> Database::grantsOf(std::uint64_t uid) const {
    std::vector<OauthGrantRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT t.client_id, IFNULL(c.name, '') AS name, "
                 "       MAX(t.created_at) AS last_at, "
                 "       SUM(CASE WHEN t.expires_at > strftime('%s','now') THEN 1 ELSE 0 END) AS active, "
                 "       MAX(t.scope) AS scope "
                 "FROM auth_tokens t LEFT JOIN oauth_clients c ON c.client_id = t.client_id "
                 "WHERE t.uid = ? AND t.kind = 3 "
                 "GROUP BY t.client_id "
                 "ORDER BY last_at DESC",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        OauthGrantRow row;
        row.client_id = columnText(stmt.raw, 0);
        row.name = columnText(stmt.raw, 1);
        if (row.name.empty()) row.name = row.client_id;
        row.last_authorized_at = sqlite3_column_int64(stmt.raw, 2);
        row.active_tokens = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 3));
        row.scope = columnText(stmt.raw, 4);
        if (row.scope.empty()) row.scope = "profile";
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint32_t Database::revokeGrants(std::uint64_t uid, const std::string& client_id) {
    if (db_ == nullptr || client_id.empty()) return 0;

    Stmt stmt;
    if (!prepare(db_,
                 "DELETE FROM auth_tokens WHERE uid = ? AND kind = 3 AND client_id = ?",
                 &stmt)) {
        return 0;
    }
    bindU64(stmt.raw, 1, uid);
    bindText(stmt.raw, 2, client_id);
    if (sqlite3_step(stmt.raw) != SQLITE_DONE) return 0;
    return static_cast<std::uint32_t>(sqlite3_changes(db_));
}

// ---------------------------------------------------------------------------
//  收藏夹
// ---------------------------------------------------------------------------
bool Database::addFavorite(std::uint64_t uid, const FavoriteRow& row) {
    if (db_ == nullptr || uid == 0 || row.msg_id.empty()) return false;

    Stmt stmt;
    // 同一条消息重复收藏时刷新快照（时间/昵称可能变），不报错
    if (!prepare(db_,
                 "INSERT INTO favorites(uid, msg_id, msg_kind, content, file_url, file_name, "
                 "file_size, from_nickname, ts, created_at) "
                 "VALUES(?,?,?,?,?,?,?,?,?,?) "
                 "ON CONFLICT(uid, msg_id) DO UPDATE SET "
                 "  msg_kind=excluded.msg_kind, content=excluded.content, "
                 "  file_url=excluded.file_url, file_name=excluded.file_name, "
                 "  file_size=excluded.file_size, from_nickname=excluded.from_nickname, "
                 "  ts=excluded.ts",
                 &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, uid);
    bindText(stmt.raw, 2, row.msg_id);
    sqlite3_bind_int(stmt.raw, 3, static_cast<int>(row.msg_kind));
    bindText(stmt.raw, 4, row.content);
    bindText(stmt.raw, 5, row.file_url);
    bindText(stmt.raw, 6, row.file_name);
    sqlite3_bind_int64(stmt.raw, 7, static_cast<sqlite3_int64>(row.file_size));
    bindText(stmt.raw, 8, row.from_nickname);
    sqlite3_bind_int64(stmt.raw, 9, static_cast<sqlite3_int64>(row.timestamp));
    sqlite3_bind_int64(stmt.raw, 10, static_cast<sqlite3_int64>(row.created_at));

    if (sqlite3_step(stmt.raw) != SQLITE_DONE) {
        spdlog::error("[db] add favorite failed: {}", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

std::vector<FavoriteRow> Database::favoritesOf(std::uint64_t uid) const {
    std::vector<FavoriteRow> rows;
    if (db_ == nullptr) return rows;

    Stmt stmt;
    if (!prepare(db_,
                 "SELECT msg_id, msg_kind, content, file_url, file_name, file_size, "
                 "from_nickname, ts, created_at "
                 "FROM favorites WHERE uid = ? ORDER BY created_at DESC, rowid DESC LIMIT 500",
                 &stmt)) {
        return rows;
    }
    bindU64(stmt.raw, 1, uid);

    while (sqlite3_step(stmt.raw) == SQLITE_ROW) {
        FavoriteRow row;
        row.msg_id = columnText(stmt.raw, 0);
        row.msg_kind = static_cast<std::uint32_t>(sqlite3_column_int(stmt.raw, 1));
        row.content = columnText(stmt.raw, 2);
        row.file_url = columnText(stmt.raw, 3);
        row.file_name = columnText(stmt.raw, 4);
        row.file_size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.raw, 5));
        row.from_nickname = columnText(stmt.raw, 6);
        row.timestamp = sqlite3_column_int64(stmt.raw, 7);
        row.created_at = sqlite3_column_int64(stmt.raw, 8);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Database::removeFavorite(std::uint64_t uid, const std::string& msg_id) {
    if (db_ == nullptr || msg_id.empty()) return false;

    Stmt stmt;
    if (!prepare(db_, "DELETE FROM favorites WHERE uid = ? AND msg_id = ?", &stmt)) {
        return false;
    }
    bindU64(stmt.raw, 1, uid);
    bindText(stmt.raw, 2, msg_id);
    return sqlite3_step(stmt.raw) == SQLITE_DONE;
}

}  // namespace im::server
