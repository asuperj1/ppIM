"""验证 COS 预签名上传是否真的可用：登录 -> 取票据 -> PUT -> 公开读回。

前置：本地服务用真实 COS 凭据启动在 18081。
用法：python lab/04_cos_ticket/verify_upload.py
"""
import asyncio
import base64
import json
import sys
import urllib.request
import uuid

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:18081/ws"

LOGIN_REQ, LOGIN_RSP = 1, 2
REGISTER_REQ, REGISTER_RSP = 5, 6
UPLOAD_REQ, UPLOAD_RSP = 41, 42

# 1x1 透明 PNG
PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
)

OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def envelope(msg_type, body):
    return json.dumps({"type": msg_type, "body": json.dumps(body)})


async def recv_until(ws, wanted, timeout=10.0):
    loop = asyncio.get_event_loop()
    deadline = loop.time() + timeout
    while True:
        left = deadline - loop.time()
        if left <= 0:
            raise TimeoutError(f"timeout waiting type={wanted}")
        raw = await asyncio.wait_for(ws.recv(), timeout=left)
        msg = json.loads(raw)
        if msg.get("type") == wanted:
            return json.loads(msg.get("body") or "{}")


async def main():
    username = "verify_" + uuid.uuid4().hex[:8]
    password = "test123456"

    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as ws:
        await ws.send(envelope(REGISTER_REQ, {
            "username": username, "password": password, "nickname": "verify",
        }))
        await recv_until(ws, REGISTER_RSP)
        await ws.send(envelope(LOGIN_REQ, {"username": username, "password": password}))
        login = await recv_until(ws, LOGIN_RSP)
        if not login.get("success"):
            print("[login] FAILED", login.get("errMsg"))
            return

        await ws.send(envelope(UPLOAD_REQ, {
            "filename": "verify.png", "size": len(PNG), "contentType": "image/png",
        }))
        ticket = await recv_until(ws, UPLOAD_RSP)
        if not ticket.get("success"):
            print("[ticket] FAILED", ticket.get("errMsg"))
            return
        print("[ticket] ok  key =", ticket.get("key"))

        put = urllib.request.Request(ticket["uploadUrl"], data=PNG, method="PUT")
        put.add_header("Content-Type", "image/png")
        try:
            with OPENER.open(put, timeout=20) as res:
                print("[put]  ", res.status, res.headers.get("ETag"))
        except urllib.error.HTTPError as e:
            body = e.read(400).decode("utf-8", "replace")
            print("[put]  FAILED", e.code, body)
            return

        try:
            with OPENER.open(ticket["fileUrl"], timeout=20) as res:
                got = res.read()
                print("[get]  ", res.status, "bytes =", len(got), "match =", got == PNG)
        except urllib.error.HTTPError as e:
            print("[get]  FAILED", e.code, "（说明桶不是公有读，或防盗链拦截）")
            return


if __name__ == "__main__":
    asyncio.run(main())
