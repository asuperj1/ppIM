"""在服务器上直连 127.0.0.1:18080/ws，抓原始帧，判断是服务端还是 Caddy 的问题。"""
import asyncio
import json

import websockets


async def main():
    async with websockets.connect("ws://127.0.0.1:18080/ws") as ws:
        await ws.send(json.dumps({"type": 1, "body": json.dumps({"username": "vtuser1", "password": "test1234"})}))
        print("sent login")
        for _ in range(2):
            raw = await asyncio.wait_for(ws.recv(), timeout=8)
            print("RSP", raw[:100])

        await ws.send(json.dumps({"type": 59, "body": "{}"}))
        print("sent favorite list")
        try:
            for _ in range(10):
                raw = await asyncio.wait_for(ws.recv(), timeout=3)
                m = json.loads(raw)
                print("type", m.get("type"), "body", (m.get("body") or "")[:100])
                if m.get("type") == 60:
                    break
        except Exception as exc:
            print("ERR", type(exc).__name__, exc)


asyncio.run(main())
