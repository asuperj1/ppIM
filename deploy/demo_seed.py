#!/usr/bin/env python3
"""pp 演示数据工具：备份 / 清空 / 注入 / 恢复。

    python3 demo_seed.py backup              # 备份到 /opt/pp/data/backups/
    python3 demo_seed.py seed                # 清空对话与动态，注入演示内容
    python3 demo_seed.py restore <备份文件>   # 回滚到某次备份

⚠️ 执行 seed / restore 前请先停服务，避免 SQLite 写冲突：

    sudo systemctl stop pp
    python3 demo_seed.py seed
    sudo systemctl start pp
"""

from __future__ import annotations

import os
import shutil
import sqlite3
import sys
import time
from datetime import datetime, timedelta

DB = "/opt/pp/data/im.db"
BACKUP_DIR = "/opt/pp/data/backups"

# ---- 演示用的账号（沿用已有用户，只改展示名）----
ME = 1          # 我：唐王
U_LIN = 2       # 林小满（设计）
U_LU = 3        # 陆一鸣（前端）
U_XU = 4        # 许知行（测试）
U_CHEN = 5      # 陈默（后端）
U_SU = 7        # 苏梨（运营）
U_ZHOU = 35     # 周远（产品）

G_PRODUCT = 1   # 产品体验群
G_DEV = 2       # pp 开发组
G_HIKE = 3      # 周末爬山小队

# ---- 复用 COS 上已有的对象（图片 / 语音 / 文件）----
IMG_HOME = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261020/54895d1e3c1944e5f1e7fade5697ac41.jpg"
IMG_ERR = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261020/261e4cafb14268d0b442a04511102245.jpg"
IMG_UI = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261020/142ff282bb9d68bb6e16aa26089de628.jpg"
IMG_HIKE = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261019/1dc6670fff09767a4906b11d83827b4b.jpg"
IMG_SCENE = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261019/ca7f9be4242ce71b3cd498777c42db1b.jpg"
IMG_EXTRA = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261019/c9f79d93d5214e108cc7d61f22cf1c6f.jpg"

VOICE_3S = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261020/88d74e46569ba62fea5c004022e2b3b9.m4a"
VOICE_4S = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261021/d3d3e00065184b2b710693720a609714.webm"

FILE_DOC = "https://im-chat-file-1486398453.cos.ap-shanghai.myqcloud.com/chat/20261019/082f9cb603dfb765229f2341e5a3a43f.docx"

NAMES = {
    U_LIN: "林小满",
    U_LU: "陆一鸣",
    U_XU: "许知行",
    U_CHEN: "陈默",
    U_SU: "苏梨",
    U_ZHOU: "周远",
}
GROUP_NAMES = {
    G_PRODUCT: "产品体验群",
    G_DEV: "pp 开发组",
    G_HIKE: "周末爬山小队",
}


def day_at(days_ago: int, hh: int, mm: int) -> int:
    """今天往前 days_ago 天的 hh:mm（本地时区）。"""
    d = datetime.now() - timedelta(days=days_ago)
    return int(d.replace(hour=hh, minute=mm, second=0, microsecond=0).timestamp())


# --------------------------------------------------------------------------
#  备份 / 恢复
# --------------------------------------------------------------------------
def backup() -> str:
    os.makedirs(BACKUP_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    dst = os.path.join(BACKUP_DIR, f"im-{stamp}.db")

    conn = sqlite3.connect(DB)
    conn.execute("PRAGMA wal_checkpoint(FULL)")
    conn.close()

    shutil.copy2(DB, dst)
    for suffix in ("-wal", "-shm"):
        src = DB + suffix
        if os.path.exists(src):
            shutil.copy2(src, dst + suffix)

    # 另存一份 SQL 文本备份：跨 SQLite 版本也能读，出问题时可人工比对
    sql_path = os.path.join(BACKUP_DIR, f"im-{stamp}.sql")
    conn = sqlite3.connect(DB)
    with open(sql_path, "w", encoding="utf-8") as fh:
        for line in conn.iterdump():
            fh.write(line + "\n")
    conn.close()

    print(f"备份完成：{dst}")
    print(f"SQL 备份：{sql_path}")
    return dst


def restore(path: str) -> None:
    if not os.path.exists(path):
        sys.exit(f"备份文件不存在：{path}")
    shutil.copy2(path, DB)
    for suffix in ("-wal", "-shm"):
        p = DB + suffix
        if os.path.exists(p):
            os.remove(p)
    print(f"已恢复：{path}")
    print("请重启 pp 服务：sudo systemctl restart pp")


# --------------------------------------------------------------------------
#  写入辅助
# --------------------------------------------------------------------------
def add_msg(conn, from_uid, to_uid, group_id, content, ts, *,
            msg_kind=1, file_url="", file_name="", file_size=0,
            quote=None, recalled=False, kind=1):
    if recalled:
        content = "[消息已撤回]"
        file_url = file_name = ""
        file_size = 0
        quote = None
    q_id, q_text, q_name = quote if quote else ("", "", "")
    cur = conn.execute(
        "INSERT INTO messages(from_uid, to_uid, group_id, content, kind, msg_kind, "
        "file_url, file_name, file_size, quote_id, quote_text, quote_nickname, recalled, created_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (from_uid, to_uid, group_id, content, kind, msg_kind, file_url, file_name,
         file_size, q_id, q_text, q_name, 1 if recalled else 0, ts),
    )
    return cur.lastrowid


def mark_read(conn, uid, peer_uid=None, gid=None) -> None:
    if gid is None:
        last = conn.execute(
            "SELECT COALESCE(MAX(id),0) FROM messages WHERE group_id = 0 AND "
            "((from_uid = ? AND to_uid = ?) OR (from_uid = ? AND to_uid = ?))",
            (uid, peer_uid, peer_uid, uid),
        ).fetchone()[0]
        conn.execute(
            "INSERT INTO read_state(uid, peer_uid, last_read_id) VALUES(?,?,?) "
            "ON CONFLICT(uid, peer_uid) DO UPDATE SET last_read_id = excluded.last_read_id",
            (uid, peer_uid, last),
        )
    else:
        last = conn.execute(
            "SELECT COALESCE(MAX(id),0) FROM messages WHERE group_id = ?", (gid,)
        ).fetchone()[0]
        conn.execute(
            "INSERT INTO group_read_state(uid, gid, last_read_id) VALUES(?,?,?) "
            "ON CONFLICT(uid, gid) DO UPDATE SET last_read_id = excluded.last_read_id",
            (uid, gid, last),
        )


# --------------------------------------------------------------------------
#  清空 + 注入
# --------------------------------------------------------------------------
def wipe(conn) -> None:
    for table in ("messages", "favorites", "posts", "post_likes", "post_comments",
                  "read_state", "group_read_state", "friend_requests"):
        conn.execute(f"DELETE FROM {table}")
    try:
        for name in ("messages", "posts", "post_comments", "friend_requests"):
            conn.execute("DELETE FROM sqlite_sequence WHERE name = ?", (name,))
    except sqlite3.Error:
        pass          # 没有 AUTOINCREMENT 表时 sqlite_sequence 不存在


def rename_for_demo(conn) -> None:
    for uid, name in NAMES.items():
        conn.execute("UPDATE users SET nickname = ? WHERE uid = ?", (name, uid))
    for gid, name in GROUP_NAMES.items():
        conn.execute("UPDATE chat_groups SET name = ? WHERE gid = ?", (name, gid))


def seed_chat(conn) -> None:
    # ============ 私聊 1：林小满（引用 / 图片 / 语音 / 撤回 / 文件 / 拍一拍）============
    home_ts = day_at(2, 21, 5)
    home_msg = add_msg(conn, U_LIN, ME, 0, "新版首页的稿子我放群里了，你有空看下间距", home_ts)
    add_msg(conn, U_LIN, ME, 0, "[图片]", home_ts + 60, msg_kind=2,
            file_url=IMG_HOME, file_name="新版首页-2.1.png", file_size=244335)
    add_msg(conn, ME, U_LIN, 0, "收到", home_ts + 180)
    add_msg(conn, ME, U_LIN, 0, "这版留白比上一版舒服，标题层级也清楚", home_ts + 240,
            quote=(str(home_msg), "新版首页的稿子我放群里了，你有空看下间距", "林小满"))

    add_msg(conn, ME, U_LIN, 0, "早上好，昨天那两个问题我改完了", day_at(1, 10, 12))
    add_msg(conn, ME, U_LIN, 0, "一个是输入框的抖动，一个是消息列表贴底", day_at(1, 10, 12) + 20)
    add_msg(conn, U_LIN, ME, 0, "这么快？", day_at(1, 10, 13))
    add_msg(conn, U_LIN, ME, 0, "[语音]", day_at(1, 10, 13) + 30, msg_kind=3,
            file_url=VOICE_3S, file_name="voice-3s.m4a", file_size=49804)
    add_msg(conn, ME, U_LIN, 0, "说错了，等下重发", day_at(1, 10, 15), recalled=True)
    add_msg(conn, U_LIN, ME, 0, "哈哈哈没事", day_at(1, 10, 16))
    add_msg(conn, ME, U_LIN, 0, "[文件] pp 接口文档.docx", day_at(1, 10, 20), msg_kind=3,
            file_url=FILE_DOC, file_name="pp 接口文档.docx", file_size=237961)

    add_msg(conn, U_LIN, ME, 0, "早上好，今天能出一版给我测吗", day_at(0, 9, 30))
    add_msg(conn, ME, U_LIN, 0, "可以，下午两点前给你", day_at(0, 9, 31))
    add_msg(conn, U_LIN, ME, 0, "唐王", day_at(0, 9, 31) + 40, msg_kind=4)   # 拍一拍

    # ============ 私聊 2：陈默（技术交流 / 语音 / 图片）============
    add_msg(conn, U_CHEN, ME, 0, "WebSocket 的重连逻辑我看了，指数退避加得挺稳", day_at(1, 15, 20))
    add_msg(conn, ME, U_CHEN, 0, "主要是断线回来会重新拉一次历史，避免中间有缺口", day_at(1, 15, 21))
    add_msg(conn, U_CHEN, ME, 0, "[语音]", day_at(1, 15, 22), msg_kind=3,
            file_url=VOICE_4S, file_name="voice-4s.webm", file_size=63036)
    add_msg(conn, ME, U_CHEN, 0, "还有个细节：服务端会把 from_uid 用会话里的 uid 覆盖，客户端传什么都不认",
            day_at(1, 15, 26))
    add_msg(conn, U_CHEN, ME, 0, "[图片]", day_at(0, 8, 50), msg_kind=2,
            file_url=IMG_ERR, file_name="报错截图.png", file_size=185292)
    add_msg(conn, U_CHEN, ME, 0, "这个报错你见过吗", day_at(0, 8, 51))
    add_msg(conn, ME, U_CHEN, 0, "COS 的 PUT 少带了 Content-Type，补上就好", day_at(0, 8, 55))

    # ============ 私聊 3：周远（故意留 2 条未读）============
    add_msg(conn, U_ZHOU, ME, 0, "演示环境准备好了吗", day_at(0, 11, 2))
    add_msg(conn, U_ZHOU, ME, 0, "下午有几个同学过来看，我直接带他们登这个", day_at(0, 11, 3))

    # ============ 群聊 1：pp 开发组（引用 / 撤回 / 拍一拍 / 图片）============
    plan_ts = day_at(1, 14, 0)
    plan_msg = add_msg(conn, ME, 0, G_DEV, "这周的迭代清单更新到文档了，大家看一下", plan_ts)
    add_msg(conn, U_LU, 0, G_DEV, "收到", plan_ts + 120)
    add_msg(conn, U_LIN, 0, G_DEV, "语音那块我来跟", plan_ts + 300,
            quote=(str(plan_msg), "这周的迭代清单更新到文档了，大家看一下", "唐王"))
    add_msg(conn, U_XU, 0, G_DEV, "测了一下，没问题", plan_ts + 480, recalled=True)
    add_msg(conn, ME, 0, G_DEV, "明天下午三点对一下进度", plan_ts + 600)
    add_msg(conn, U_LIN, 0, G_DEV, "唐王", plan_ts + 660, msg_kind=4)          # 拍一拍
    add_msg(conn, U_XU, 0, G_DEV, "[图片]", day_at(0, 10, 30), msg_kind=2,
            file_url=IMG_UI, file_name="首页改版.png", file_size=230951)
    add_msg(conn, U_XU, 0, G_DEV, "首页改完的效果", day_at(0, 10, 31))

    # ============ 群聊 2：产品体验群 ============
    add_msg(conn, U_LIN, 0, G_PRODUCT, "新版体验反馈表我发群里了，大家随手填一下", day_at(1, 9, 0))
    add_msg(conn, U_LU, 0, G_PRODUCT, "好", day_at(1, 9, 5))
    add_msg(conn, ME, 0, G_PRODUCT, "有问题直接在群里说也行", day_at(1, 9, 6))

    # ============ 群聊 3：周末爬山小队（图片）============
    add_msg(conn, U_SU, 0, G_HIKE, "这周六天气不错，有人去爬香山吗", day_at(2, 19, 0))
    add_msg(conn, U_CHEN, 0, G_HIKE, "算我一个", day_at(2, 19, 5))
    add_msg(conn, ME, 0, G_HIKE, "[图片]", day_at(2, 19, 8), msg_kind=2,
            file_url=IMG_HIKE, file_name="上次的山顶.jpg", file_size=154048)
    add_msg(conn, ME, 0, G_HIKE, "上次拍的，不知道这周叶子红了没", day_at(2, 19, 8) + 10)
    add_msg(conn, U_SU, 0, G_HIKE, "八点地铁口集合？", day_at(2, 19, 10))

    # ============ 已读位点 ============
    for peer in (U_LIN, U_CHEN):
        mark_read(conn, ME, peer_uid=peer)      # 周远故意不设置 —— 留 2 条未读
    for gid in (G_DEV, G_PRODUCT, G_HIKE):
        mark_read(conn, ME, gid=gid)


def seed_feed(conn) -> None:
    def add_post(uid, content, images, ts, likers, comments):
        cur = conn.execute(
            "INSERT INTO posts(uid, content, images, deleted, created_at) VALUES(?,?,?,0,?)",
            (uid, content, "\n".join(images), ts),
        )
        post_id = cur.lastrowid
        for i, liker in enumerate(likers):
            conn.execute(
                "INSERT OR IGNORE INTO post_likes(post_id, uid, created_at) VALUES(?,?,?)",
                (post_id, liker, ts + 60 * (i + 1)),
            )
        for i, (commenter, text) in enumerate(comments):
            conn.execute(
                "INSERT INTO post_comments(post_id, uid, content, created_at) VALUES(?,?,?,?)",
                (post_id, commenter, text, ts + 300 * (i + 1)),
            )
        return post_id

    add_post(
        U_LIN,
        "把首页的留白重新调了一遍，眼睛终于不累了。",
        [IMG_UI],
        day_at(1, 21, 30),
        [U_CHEN, U_ZHOU, ME],
        [(U_CHEN, "确实通透"), (ME, "这版可以定稿了")],
    )
    add_post(
        U_CHEN,
        "终于搞清楚了：移动端进会话滚不到底，是因为元素还在 display:none 时读的 scrollHeight。",
        [],
        day_at(1, 16, 40),
        [ME, U_XU],
        [(U_XU, "经典 DOM 时序问题")],
    )
    add_post(
        U_SU,
        "周末爬山，山顶风很大，但视野值回票价。",
        [IMG_HIKE],
        day_at(2, 19, 30),
        [U_LIN, U_CHEN, ME],
        [],
    )
    add_post(
        ME,
        "pp 的语音输入终于能用了，说的什么就转什么。",
        [IMG_HOME],
        day_at(0, 9, 40),
        [U_LIN, U_CHEN, U_ZHOU, U_SU],
        [(U_LIN, "试了下识别挺准")],
    )
    add_post(
        U_ZHOU,
        "整理了一版体验走查清单，明天发给大家。",
        [IMG_EXTRA],
        day_at(2, 10, 15),
        [ME],
        [(U_LU, "等你的清单")],
    )


def seed() -> None:
    conn = sqlite3.connect(DB)
    try:
        with conn:
            wipe(conn)
            rename_for_demo(conn)
            seed_chat(conn)
            seed_feed(conn)
    finally:
        conn.close()

    conn = sqlite3.connect(DB)
    stats = {
        "messages": conn.execute("SELECT COUNT(*) FROM messages").fetchone()[0],
        "posts": conn.execute("SELECT COUNT(*) FROM posts").fetchone()[0],
        "likes": conn.execute("SELECT COUNT(*) FROM post_likes").fetchone()[0],
        "comments": conn.execute("SELECT COUNT(*) FROM post_comments").fetchone()[0],
    }
    conn.close()
    print("注入完成：", stats)


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    action = sys.argv[1]
    if action == "backup":
        backup()
    elif action == "seed":
        seed()
    elif action == "restore":
        if len(sys.argv) < 3:
            sys.exit("用法：demo_seed.py restore <备份文件>")
        restore(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
