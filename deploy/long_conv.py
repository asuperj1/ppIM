"""临时构造一个长会话，用于验证「滚到顶部自动加载更早消息」。

    python3 long_conv.py seed     # 插入 45 条 uid1 <-> uid3 的消息
    python3 long_conv.py clean    # 删除这些消息与已读位点
"""

import sqlite3
import sys
import time

DB = "/opt/pp/data/im.db"
MARK = "__LONGTEST__"


def seed() -> None:
    conn = sqlite3.connect(DB)
    now = int(time.time())
    rows = []
    base = now - 45 * 3600
    for i in range(45):
        ts = base + i * 600
        if i % 2 == 0:
            rows.append((3, 1, f"{MARK} 陆一鸣的第 {i + 1} 条消息，用来测试向上加载历史", ts))
        else:
            rows.append((1, 3, f"{MARK} 唐王的第 {i + 1} 条回复，同样用于测试分页", ts))
    conn.executemany(
        "INSERT INTO messages(from_uid, to_uid, group_id, content, kind, msg_kind, "
        "file_url, file_name, file_size, quote_id, quote_text, quote_nickname, recalled, created_at) "
        "VALUES(?,?,0,?,1,1,'','',0,'','','',0,?)",
        rows,
    )
    # 把已读位点推到最新，确保看不到红点干扰
    last = conn.execute("SELECT MAX(id) FROM messages").fetchone()[0]
    conn.execute(
        "INSERT INTO read_state(uid, peer_uid, last_read_id) VALUES(1, 3, ?) "
        "ON CONFLICT(uid, peer_uid) DO UPDATE SET last_read_id = excluded.last_read_id",
        (last,),
    )
    conn.commit()
    total = conn.execute(
        "SELECT COUNT(*) FROM messages WHERE content LIKE ?", (MARK + "%",)
    ).fetchone()[0]
    conn.close()
    print("inserted", total, "messages, max id", last)


def clean() -> None:
    conn = sqlite3.connect(DB)
    cur = conn.execute("DELETE FROM messages WHERE content LIKE ?", (MARK + "%",))
    conn.execute("DELETE FROM read_state WHERE uid = 1 AND peer_uid = 3")
    conn.commit()
    deleted = cur.rowcount
    conn.close()
    print("deleted", deleted)


if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else ""
    if action == "seed":
        seed()
    elif action == "clean":
        clean()
    else:
        sys.exit(__doc__)
