import asyncio
import json

import websockets


async def main():
    async with websockets.connect("ws://127.0.0.1:8080/ws") as ws:
        print("connected")
        await ws.send(json.dumps({"type": 4, "body": "{}"}))
        try:
            for i in range(3):
                msg = await asyncio.wait_for(ws.recv(), timeout=3)
                print(f"recv[{i}] type={type(msg).__name__} len={len(msg)} head={msg[:120]!r}")
        except asyncio.TimeoutError:
            print("timeout waiting for message")
            return

        # 试一次会话恢复（令牌由测试脚本导出）
        token = ""
        try:
            with open("lab/10_oauth/token.txt", encoding="utf-8") as f:
                token = f.read().strip()
        except OSError:
            pass
        if token:
            await ws.send(json.dumps({"type": 55, "body": json.dumps({"token": token})}))
            msg = await asyncio.wait_for(ws.recv(), timeout=3)
            print(f"session rsp: {msg[:200]}")


asyncio.run(main())
