"""检查演示数据的归属：确认所有内容都在 pp100001（uid=1）名下。"""

import sqlite3

conn = sqlite3.connect("/opt/pp/data/im.db")

print("== pp100001 (uid 1) ==")
print(conn.execute("SELECT uid, username, nickname, pp_id FROM users WHERE uid = 1").fetchone())

print("\n== 私聊会话（参与者）==")
for row in conn.execute(
        "SELECT CASE WHEN from_uid = 1 THEN to_uid ELSE from_uid END AS peer, COUNT(*) "
        "FROM messages WHERE group_id = 0 AND (from_uid = 1 OR to_uid = 1) "
        "GROUP BY peer ORDER BY peer"):
    print("   peer uid", row[0], "messages", row[1])

print("\n== 其他用户之间的私聊（应为 0 行）==")
rows = conn.execute(
    "SELECT COUNT(*) FROM messages WHERE group_id = 0 AND from_uid <> 1 AND to_uid <> 1"
).fetchone()[0]
print("   ", rows)

print("\n== pp100001 所在的群 ==")
for row in conn.execute(
        "SELECT g.gid, g.name, "
        "(SELECT COUNT(*) FROM messages m WHERE m.group_id = g.gid) AS msgs "
        "FROM chat_groups g JOIN group_members gm ON gm.gid = g.gid AND gm.uid = 1"):
    print("   gid", row[0], row[1], "messages", row[2])

print("\n== 朋友圈作者 ==")
for row in conn.execute(
        "SELECT p.uid, u.nickname, COUNT(*) FROM posts p JOIN users u ON u.uid = p.uid "
        "GROUP BY p.uid"):
    print("   ", row)

print("\n== pp100001 未读 ==")
for row in conn.execute(
        "SELECT m.from_uid, COUNT(*) AS unread FROM messages m "
        "WHERE m.group_id = 0 AND m.to_uid = 1 "
        "AND m.id > COALESCE((SELECT last_read_id FROM read_state r "
        "                     WHERE r.uid = 1 AND r.peer_uid = m.from_uid), 0) "
        "GROUP BY m.from_uid"):
    print("   peer", row[0], "unread", row[1])
