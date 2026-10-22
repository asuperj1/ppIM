"""线上验证：注册后改用 pp 号登录。"""
import asyncio
import json
import sys
import uuid

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "wss://pp.xiaojietong.online/ws"


def env(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def recv(ws, wanted, timeout=8):
    loop = asyncio.get_event_loop()
    end = loop.time() + timeout
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=end - loop.time())
        m = json.loads(raw)
        if m.get("type") == wanted:
            return json.loads(m.get("body") or "{}")


async def main():
    name = "logintest_" + uuid.uuid4().hex[:6]
    async with websockets.connect(URL) as ws:
        await ws.send(env(5, {"username": name, "password": "test123456", "nickname": "登录测试"}))
        reg = await recv(ws, 6)
        pp = reg.get("ppId")
        print("[register]", reg.get("success"), pp)

    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": pp, "password": "test123456"}))
        rsp = await recv(ws, 2)
        print("[login by pp]", rsp.get("success"), rsp.get("nickname"), rsp.get("ppId"))

    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": name, "password": "test123456"}))
        rsp = await recv(ws, 2)
        print("[login by username]", rsp.get("success"), rsp.get("ppId"))


asyncio.run(main())
