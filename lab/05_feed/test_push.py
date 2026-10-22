"""验证：新动态实时推送 POST_PUSH(53) + 按作者筛选时间线。"""
import asyncio
import json
import sys
import uuid

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:18083/ws"

LOGIN_REQ, LOGIN_RSP = 1, 2
REGISTER_REQ, REGISTER_RSP = 5, 6
FRIEND_REQUEST_REQ = 19
FRIEND_REQUEST_PUSH = 21
FRIEND_HANDLE_REQ, FRIEND_HANDLE_RSP = 24, 25
POST_CREATE_REQ, POST_CREATE_RSP = 43, 44
FEED_REQ, FEED_RSP = 45, 46
POST_PUSH = 53


def envelope(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def recv_until(ws, wanted, timeout=5.0):
    loop = asyncio.get_event_loop()
    deadline = loop.time() + timeout
    while True:
        left = deadline - loop.time()
        if left <= 0:
            raise TimeoutError(f"timeout type={wanted}")
        raw = await asyncio.wait_for(ws.recv(), timeout=left)
        msg = json.loads(raw)
        if msg.get("type") == wanted:
            return json.loads(msg.get("body") or "{}")


async def signup(ws, tag):
    name = f"push_{tag}_" + uuid.uuid4().hex[:8]
    await ws.send(envelope(REGISTER_REQ, {"username": name, "password": "test123456", "nickname": "推送" + tag}))
    await recv_until(ws, REGISTER_RSP)
    await ws.send(envelope(LOGIN_REQ, {"username": name, "password": "test123456"}))
    return await recv_until(ws, LOGIN_RSP)


async def main():
    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as a, \
            websockets.connect(URL, max_size=8 * 1024 * 1024) as b, \
            websockets.connect(URL, max_size=8 * 1024 * 1024) as c:
        ua = await signup(a, "a")
        ub = await signup(b, "b")
        uc = await signup(c, "c")

        await a.send(envelope(FRIEND_REQUEST_REQ, {"toUid": ub["uid"], "message": "hi"}))
        push = await recv_until(b, FRIEND_REQUEST_PUSH)
        rid = push["request"]["requestId"]
        await b.send(envelope(FRIEND_HANDLE_REQ, {"requestId": rid, "accept": True}))
        await recv_until(b, FRIEND_HANDLE_RSP)
        print("[friend] ok")

        await a.send(envelope(POST_CREATE_REQ, {"content": "推送测试动态", "images": []}))
        created = await recv_until(a, POST_CREATE_RSP)
        post_id = (created.get("post") or {}).get("id")
        print("[create]", created.get("success"), post_id)

        pushed = await recv_until(b, POST_PUSH, timeout=4)
        print("[push to friend]", pushed.get("content"), "mine =", pushed.get("mine"))

        try:
            await recv_until(c, POST_PUSH, timeout=2)
            print("[push to stranger] BUG: 陌生人收到了推送")
        except TimeoutError:
            print("[push to stranger] none (correct)")

        await b.send(envelope(FEED_REQ, {"beforeId": 0, "limit": 10, "authorUid": int(ua["uid"])}))
        feed = await recv_until(b, FEED_RSP)
        posts = feed.get("posts") or []
        print("[author feed]", feed.get("authorUid"), len(posts), posts[0].get("content") if posts else None)

        await b.send(envelope(FEED_REQ, {"beforeId": 0, "limit": 10}))
        main_feed = await recv_until(b, FEED_RSP)
        print("[main feed]", main_feed.get("authorUid"), len(main_feed.get("posts") or []))


asyncio.run(main())
