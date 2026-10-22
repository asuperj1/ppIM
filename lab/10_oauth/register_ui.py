"""注册一个浏览器联调用的临时 pp 账号。"""
import asyncio
import json
import sys

import websockets

USERNAME = sys.argv[1]
PASSWORD = sys.argv[2]


async def main():
    async with websockets.connect("wss://pp.xiaojietong.online/ws", open_timeout=10) as ws:
        payload = {"username": USERNAME, "password": PASSWORD, "nickname": "浏览器测试号"}
        await ws.send(json.dumps({"type": 5, "body": json.dumps(payload)}))
        for _ in range(8):
            raw = await asyncio.wait_for(ws.recv(), timeout=10)
            msg = json.loads(raw)
            if int(msg.get("type", 0)) == 6:
                print(msg.get("body"))
                return


asyncio.run(main())
