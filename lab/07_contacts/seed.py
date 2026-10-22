"""为联系人页准备测试数据：好友、待处理申请、群聊。"""
import asyncio
import json

import websockets

URL = "ws://127.0.0.1:8080/ws"

REG, REG_RSP = 5, 6
LOGIN, LOGIN_RSP = 1, 2
FRIEND_REQ = 19
FRIEND_LIST = 22
FRIEND_HANDLE = 24
FRIEND_HANDLE_RSP = 25
GROUP_CREATE, GROUP_CREATE_RSP = 27, 28


def env(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def recv(ws, wanted, timeout=5):
    loop = asyncio.get_event_loop()
    end = loop.time() + timeout
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=end - loop.time())
        m = json.loads(raw)
        if m.get("type") == wanted:
            return json.loads(m.get("body") or "{}")


async def signup(ws, username, nickname, password="test123456"):
    await ws.send(env(REG, {"username": username, "password": password, "nickname": nickname}))
    await recv(ws, REG_RSP)
    await ws.send(env(LOGIN, {"username": username, "password": password}))
    return await recv(ws, LOGIN_RSP)


async def login(ws, username, password="test123456"):
    await ws.send(env(LOGIN, {"username": username, "password": password}))
    return await recv(ws, LOGIN_RSP)


async def fetch_request(ws, from_uid, tries=4):
    for _ in range(tries):
        await asyncio.sleep(0.4)
        await ws.send(env(FRIEND_LIST, {}))
        deadline = asyncio.get_event_loop().time() + 3
        while asyncio.get_event_loop().time() < deadline:
            body = await recv(ws, 23)
            for r in body.get("requests") or []:
                if r.get("fromUid") == from_uid:
                    return r
    return None


async def main():
    async with websockets.connect(URL) as a, websockets.connect(URL) as b, websockets.connect(URL) as c:
        ua = await login(a, "uicontact")
        ub = await login(b, "friend1")
        uc = await login(c, "friend2")
        print("[users]", ua.get("uid"), ub.get("uid"), uc.get("uid"))

        await b.send(env(FRIEND_REQ, {"toUid": ua["uid"], "message": "我是新约能天使"}))
        await c.send(env(FRIEND_REQ, {"toUid": ua["uid"], "message": "我是艾草笔"}))
        await asyncio.sleep(0.5)

        pending = await fetch_request(a, uc["uid"])
        if pending:
            await a.send(env(FRIEND_HANDLE, {"requestId": pending["requestId"], "accept": True}))
            print("[accept]", await recv(a, FRIEND_HANDLE_RSP))
        else:
            print("[accept] not found")

        await asyncio.sleep(0.4)
        await a.send(env(GROUP_CREATE, {"name": "周末爬山计划", "memberUids": [uc["uid"]]}))
        created = await recv(a, GROUP_CREATE_RSP)
        print("[group]", created.get("success"), (created.get("group") or {}).get("gid"))


asyncio.run(main())
