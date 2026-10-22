"""二维码联调：准备两个账号，供浏览器登录与扫码使用。"""
import asyncio
import json

import websockets

URL = "ws://127.0.0.1:8080/ws"


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


async def make(ws, username, nickname):
    await ws.send(env(5, {"username": username, "password": "test123456", "nickname": nickname}))
    reg = await recv(ws, 6)
    await ws.send(env(1, {"username": username, "password": "test123456"}))
    login = await recv(ws, 2)
    print(username, "->", login.get("ppId"), "uid", login.get("uid"))


async def main():
    async with websockets.connect(URL) as a, websockets.connect(URL) as b:
        await make(a, "qrtest1", "唐王")
        await make(b, "qrtest2", "新约能天使")


asyncio.run(main())
