"""调试：直接打印动态相关消息的原始 envelope。"""
import asyncio
import json
import sys
import uuid

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "wss://pp.xiaojietong.online/ws"


def envelope(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def main():
    name = "dbg_" + uuid.uuid4().hex[:8]
    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as ws:
        await ws.send(envelope(5, {"username": name, "password": "test123456", "nickname": "dbg"}))
        print("[reg]", await ws.recv())
        await ws.send(envelope(1, {"username": name, "password": "test123456"}))
        while True:
            raw = await ws.recv()
            msg = json.loads(raw)
            if msg.get("type") == 2:
                print("[login]", msg)
                break

        await ws.send(envelope(43, {"content": "纯文字动态", "images": []}))
        for _ in range(3):
            raw = await asyncio.wait_for(ws.recv(), timeout=5)
            print("[raw]", raw)
            if json.loads(raw).get("type") == 44:
                break

        await ws.send(envelope(43, {
            "content": "带外链图片", "images": ["https://example.com/a.jpg"],
        }))
        for _ in range(3):
            raw = await asyncio.wait_for(ws.recv(), timeout=5)
            print("[raw2]", raw)
            if json.loads(raw).get("type") == 44:
                break

        await ws.send(envelope(45, {"beforeId": 0, "limit": 5}))
        for _ in range(3):
            raw = await asyncio.wait_for(ws.recv(), timeout=5)
            print("[raw3]", raw)
            if json.loads(raw).get("type") == 46:
                break


asyncio.run(main())
