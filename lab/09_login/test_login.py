"""验证：用户名 / pp 号都能登录。"""
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


async def main():
    async with websockets.connect(URL) as ws:
        await ws.send(env(5, {"username": "loginuser", "password": "test123456", "nickname": "唐王"}))
        await recv(ws, 6)
        await ws.send(env(1, {"username": "loginuser", "password": "test123456"}))
        first = await recv(ws, 2)
        print("[by username]", first.get("success"), first.get("ppId"))
        pp = first.get("ppId")

    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": pp, "password": "test123456"}))
        second = await recv(ws, 2)
        print("[by pp]", second.get("success"), second.get("nickname"), second.get("ppId"))

    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": pp.upper(), "password": "test123456"}))
        third = await recv(ws, 2)
        print("[by PP uppercase]", third.get("success"), third.get("ppId"))

    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": pp, "password": "wrongpass"}))
        bad = await recv(ws, 2)
        print("[wrong password]", bad.get("success"), bad.get("errMsg"))


asyncio.run(main())
