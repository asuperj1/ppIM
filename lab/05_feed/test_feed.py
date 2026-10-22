"""动态（朋友圈）接口联调：发帖 / 时间线 / 点赞 / 评论 / 删除 / 权限。

前置：本地服务运行在 18082（不配 COS 环境变量，方便用任意图片 URL）。
用法：python lab/05_feed/test_feed.py
"""
import asyncio
import json
import sys
import uuid

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:18082/ws"

LOGIN_REQ, LOGIN_RSP = 1, 2
REGISTER_REQ, REGISTER_RSP = 5, 6
FRIEND_HANDLE_REQ, FRIEND_HANDLE_RSP = 24, 25
FRIEND_REQUEST_REQ = 19
FRIEND_REQUEST_PUSH = 21
ERROR_PUSH = 13
POST_CREATE_REQ, POST_CREATE_RSP = 43, 44
FEED_REQ, FEED_RSP = 45, 46
LIKE_REQ, LIKE_RSP = 47, 48
COMMENT_REQ, COMMENT_RSP = 49, 50
DELETE_REQ, DELETE_RSP = 51, 52


def envelope(msg_type, body):
    return json.dumps({"type": msg_type, "body": json.dumps(body)})


async def recv_until(ws, wanted, timeout=5.0, predicate=None):
    loop = asyncio.get_event_loop()
    deadline = loop.time() + timeout
    while True:
        left = deadline - loop.time()
        if left <= 0:
            raise TimeoutError(f"timeout waiting type={wanted}")
        raw = await asyncio.wait_for(ws.recv(), timeout=left)
        msg = json.loads(raw)
        if msg.get("type") != wanted:
            continue
        body = json.loads(msg.get("body") or "{}")
        if predicate is None or predicate(body):
            return body


async def signup(ws, tag):
    username = f"feed_{tag}_" + uuid.uuid4().hex[:8]
    await ws.send(envelope(REGISTER_REQ, {
        "username": username, "password": "test123456", "nickname": "动态" + tag,
    }))
    await recv_until(ws, REGISTER_RSP)
    await ws.send(envelope(LOGIN_REQ, {"username": username, "password": "test123456"}))
    return await recv_until(ws, LOGIN_RSP)


async def befriend(a, b, b_uid):
    await a.send(envelope(FRIEND_REQUEST_REQ, {"toUid": b_uid, "message": "hi"}))
    push = await recv_until(b, FRIEND_REQUEST_PUSH)
    rid = (push.get("request") or {}).get("requestId")
    await b.send(envelope(FRIEND_HANDLE_REQ, {"requestId": rid, "accept": True}))
    await recv_until(b, FRIEND_HANDLE_RSP)


async def main():
    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as alice, \
            websockets.connect(URL, max_size=8 * 1024 * 1024) as bob, \
            websockets.connect(URL, max_size=8 * 1024 * 1024) as carol:

        a = await signup(alice, "a")
        b = await signup(bob, "b")
        c = await signup(carol, "c")
        print("[users]", a["uid"], b["uid"], c["uid"])

        await befriend(alice, bob, b["uid"])
        print("[friend] alice <-> bob ok")

        # 1) 发帖：文字 + 2 张图
        await alice.send(envelope(POST_CREATE_REQ, {
            "content": "今天天气不错，出来走走 ☀️",
            "images": ["https://example.com/a.jpg", "https://example.com/b.jpg"],
        }))
        created = await recv_until(alice, POST_CREATE_RSP)
        post = created.get("post") or {}
        post_id = post.get("id")
        print("[post]", created.get("success"), post_id, len(post.get("images") or []))

        # 2) 空内容应被拒绝
        await alice.send(envelope(POST_CREATE_REQ, {"content": "  ", "images": []}))
        bad = await recv_until(alice, POST_CREATE_RSP)
        print("[empty post]", bad.get("success"), bad.get("errMsg"))

        # 3) 好友能在时间线看到
        await bob.send(envelope(FEED_REQ, {"beforeId": 0, "limit": 20}))
        feed = await recv_until(bob, FEED_RSP)
        posts = feed.get("posts") or []
        print("[feed bob]", len(posts), posts[0].get("content") if posts else None,
              posts[0].get("nickname") if posts else None,
              posts[0].get("liked") if posts else None)

        # 4) 陌生人看不到
        await carol.send(envelope(FEED_REQ, {"beforeId": 0, "limit": 20}))
        cf = await recv_until(carol, FEED_RSP)
        print("[feed carol]", len(cf.get("posts") or []))

        # 5) 点赞
        await bob.send(envelope(LIKE_REQ, {"postId": post_id, "like": True}))
        liked = await recv_until(bob, LIKE_RSP)
        print("[like]", liked.get("success"), liked.get("liked"), liked.get("likeCount"))

        await bob.send(envelope(LIKE_REQ, {"postId": post_id, "like": False}))
        unliked = await recv_until(bob, LIKE_RSP)
        print("[unlike]", unliked.get("liked"), unliked.get("likeCount"))

        # 6) 评论
        await bob.send(envelope(COMMENT_REQ, {"postId": post_id, "content": "确实不错"}))
        commented = await recv_until(bob, COMMENT_RSP)
        print("[comment]", commented.get("success"),
              (commented.get("comment") or {}).get("nickname"),
              commented.get("commentCount"))

        # 7) 刷新时间线看计数
        await bob.send(envelope(FEED_REQ, {"beforeId": 0, "limit": 20}))
        feed2 = await recv_until(bob, FEED_RSP)
        p0 = (feed2.get("posts") or [{}])[0]
        print("[feed after]", p0.get("likeCount"), p0.get("commentCount"),
              len(p0.get("comments") or []))

        # 8) 陌生人不能点赞
        await carol.send(envelope(LIKE_REQ, {"postId": post_id, "like": True}))
        deny = await recv_until(carol, LIKE_RSP)
        print("[deny like]", deny.get("success"), deny.get("errMsg"))

        # 9) 非作者不能删
        await bob.send(envelope(DELETE_REQ, {"postId": post_id}))
        deny_del = await recv_until(bob, DELETE_RSP)
        print("[deny delete]", deny_del.get("success"), deny_del.get("errMsg"))

        # 10) 作者删除
        await alice.send(envelope(DELETE_REQ, {"postId": post_id}))
        deleted = await recv_until(alice, DELETE_RSP)
        print("[delete]", deleted.get("success"))

        await bob.send(envelope(FEED_REQ, {"beforeId": 0, "limit": 20}))
        feed3 = await recv_until(bob, FEED_RSP)
        print("[feed after delete]", len(feed3.get("posts") or []))


if __name__ == "__main__":
    asyncio.run(main())
