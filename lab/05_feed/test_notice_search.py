"""验证：精确 pp 号搜索 + 点赞/评论通知。"""
import asyncio
import json
import uuid

import websockets

URL = "ws://127.0.0.1:18084/ws"

LOGIN_REQ, LOGIN_RSP = 1, 2
REGISTER_REQ, REGISTER_RSP = 5, 6
SEARCH_REQ, SEARCH_RSP = 17, 18
FRIEND_REQUEST_REQ = 19
FRIEND_REQUEST_PUSH = 21
FRIEND_HANDLE_REQ, FRIEND_HANDLE_RSP = 24, 25
POST_CREATE_REQ, POST_CREATE_RSP = 43, 44
FEED_REQ, FEED_RSP = 45, 46
LIKE_REQ, LIKE_RSP = 47, 48
COMMENT_REQ, COMMENT_RSP = 49, 50
POST_NOTICE = 54


def envelope(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def recv_until(ws, wanted, timeout=5.0, predicate=None):
    loop = asyncio.get_event_loop()
    deadline = loop.time() + timeout
    while True:
        left = deadline - loop.time()
        if left <= 0:
            raise TimeoutError(f"timeout type={wanted}")
        raw = await asyncio.wait_for(ws.recv(), timeout=left)
        msg = json.loads(raw)
        if msg.get("type") != wanted:
            continue
        body = json.loads(msg.get("body") or "{}")
        if predicate is None or predicate(body):
            return body


async def signup(ws, tag):
    name = f"ex_{tag}_" + uuid.uuid4().hex[:8]
    await ws.send(envelope(REGISTER_REQ, {"username": name, "password": "test123456", "nickname": "精确" + tag}))
    reg = await recv_until(ws, REGISTER_RSP)
    await ws.send(envelope(LOGIN_REQ, {"username": name, "password": "test123456"}))
    login = await recv_until(ws, LOGIN_RSP)
    return reg.get("ppId"), login


async def main():
    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as a, \
            websockets.connect(URL, max_size=8 * 1024 * 1024) as b:
        pp_a, ua = await signup(a, "a")
        pp_b, ub = await signup(b, "b")
        print("[users]", pp_a, pp_b)

        # --- 精确搜索 ---
        for keyword in [pp_a[:-1], pp_a.lower()[2:], "精确a", pp_a.upper()]:
            await b.send(envelope(SEARCH_REQ, {"keyword": keyword}))
            rsp = await recv_until(b, SEARCH_RSP)
            print(f"[search {keyword!r}]", len(rsp.get("users") or []))

        await b.send(envelope(SEARCH_REQ, {"keyword": pp_a}))
        rsp = await recv_until(b, SEARCH_RSP)
        users = rsp.get("users") or []
        print("[search exact]", len(users), users[0].get("ppId") if users else None)

        # --- 加好友 ---
        await a.send(envelope(FRIEND_REQUEST_REQ, {"toUid": ub["uid"], "message": "hi"}))
        push = await recv_until(b, FRIEND_REQUEST_PUSH)
        await b.send(envelope(FRIEND_HANDLE_REQ, {"requestId": push["request"]["requestId"], "accept": True}))
        await recv_until(b, FRIEND_HANDLE_RSP)

        # --- 发帖 ---
        await a.send(envelope(POST_CREATE_REQ, {"content": "通知测试", "images": []}))
        created = await recv_until(a, POST_CREATE_RSP)
        post_id = int(created["post"]["id"])
        print("[post]", post_id)

        # --- B 点赞 -> A 收到通知 kind=1 ---
        await b.send(envelope(LIKE_REQ, {"postId": post_id, "like": True}))
        await recv_until(b, LIKE_RSP)
        n1 = await recv_until(a, POST_NOTICE, timeout=4)
        print("[notice like]", n1.get("kind"), n1.get("fromNickname"), n1.get("likeCount"))

        # --- B 评论 -> A 收到通知 kind=2 ---
        await b.send(envelope(COMMENT_REQ, {"postId": post_id, "content": "不错"}))
        await recv_until(b, COMMENT_RSP)
        n2 = await recv_until(a, POST_NOTICE, timeout=4)
        print("[notice comment]", n2.get("kind"), n2.get("content"), n2.get("commentCount"))

        # --- B 自己赞自己的帖子不应收到通知（此处 B 无帖子，跳过）---
        # --- A 赞自己的帖子不应通知 ---
        await a.send(envelope(LIKE_REQ, {"postId": post_id, "like": True}))
        await recv_until(a, LIKE_RSP)
        try:
            await recv_until(a, POST_NOTICE, timeout=1.5)
            print("[self notice] BUG 自己赞自己收到通知")
        except TimeoutError:
            print("[self notice] none (correct)")


asyncio.run(main())
