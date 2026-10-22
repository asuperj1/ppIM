"""头像上传全链路：取票据 -> PUT -> 保存资料 -> 重新登录校验。"""
import asyncio
import base64
import json
import sys
import urllib.request
import uuid

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:18085/ws"

LOGIN_REQ, LOGIN_RSP = 1, 2
REGISTER_REQ, REGISTER_RSP = 5, 6
PROFILE_REQ, PROFILE_RSP = 15, 16
UPLOAD_REQ, UPLOAD_RSP = 41, 42

PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAGQAAABkCAIAAAD/gAIDAAAAWklEQVR42u3QMQEAAAjDMMC/56EBHnUQyN1JggYNGjRo0KBBgwYNGjRo0KBBgwYNGjRo0KBBgwYNGjRo0KBBgwYNGjRo0KBBgwYNGjRo0KBBgwYNGjRo0KBBgwYNGjRI0KsBAQEAAR0lSGMAAAAASUVORK5CYII="
)
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def envelope(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def recv_until(ws, wanted, timeout=10.0):
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


async def main():
    name = "ava_" + uuid.uuid4().hex[:8]

    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as ws:
        await ws.send(envelope(REGISTER_REQ, {
            "username": name, "password": "test123456", "nickname": "头像",
        }))
        await recv_until(ws, REGISTER_RSP)

        await ws.send(envelope(LOGIN_REQ, {"username": name, "password": "test123456"}))
        login = await recv_until(ws, LOGIN_RSP)
        print("[login]", login.get("success"), "avatar =", repr(login.get("avatarUrl")))

        await ws.send(envelope(UPLOAD_REQ, {
            "filename": "avatar.png", "size": len(PNG), "contentType": "image/png",
        }))
        ticket = await recv_until(ws, UPLOAD_RSP)
        if not ticket.get("success"):
            print("[ticket] FAILED", ticket.get("errMsg"))
            return

        req = urllib.request.Request(ticket["uploadUrl"], data=PNG, method="PUT")
        req.add_header("Content-Type", "image/png")
        with OPENER.open(req, timeout=20) as res:
            print("[put]", res.status)

        head = urllib.request.Request(ticket["fileUrl"], method="HEAD")
        with OPENER.open(head, timeout=20) as res:
            print("[head]", res.status, res.headers.get("Content-Type"))

        await ws.send(envelope(PROFILE_REQ, {
            "nickname": "头像", "signature": "换头像啦", "ppId": "",
            "avatarUrl": ticket["fileUrl"],
        }))
        saved = await recv_until(ws, PROFILE_RSP)
        print("[save]", saved.get("success"), saved.get("errMsg"))

        await ws.send(envelope(PROFILE_REQ, {
            "nickname": "头像", "signature": "", "ppId": "",
            "avatarUrl": "https://evil.example.com/a.png",
        }))
        bad = await recv_until(ws, PROFILE_RSP)
        print("[reject foreign]", bad.get("success"), bad.get("errMsg"))

    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as ws2:
        await ws2.send(envelope(LOGIN_REQ, {"username": name, "password": "test123456"}))
        relogin = await recv_until(ws2, LOGIN_RSP)
        print("[relogin avatar]", relogin.get("avatarUrl"))


asyncio.run(main())
