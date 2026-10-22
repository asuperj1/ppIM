"""验证 pp 服务端已接受语音文件类型：向 UPLOAD_TICKET_REQ 申请 voice-5s.webm 的票据。

用法：python lab/11_voice/test_voice_ticket.py
"""
import asyncio
import json
import sys

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "wss://pp.xiaojietong.online/ws"
USER, PWD = "vtuser1", "test1234"


def env(t, body):
    return json.dumps({"type": t, "body": json.dumps(body)})


async def recv(ws, wanted, timeout=10):
    loop = asyncio.get_event_loop()
    end = loop.time() + timeout
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=end - loop.time())
        m = json.loads(raw)
        if m.get("type") == wanted:
            return json.loads(m.get("body") or "{}")


async def main():
    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": USER, "password": PWD}))
        rsp = await recv(ws, 2)
        if not rsp.get("success"):
            raise SystemExit("登录失败：" + str(rsp.get("errMsg")))
        print("[login] ok uid=", rsp.get("uid"))

        cases = [
            ("voice-5s.webm", "audio/webm", 4096),
            ("voice-3s.m4a", "audio/mp4", 4096),
            ("voice-9s.ogg", "audio/ogg", 4096),
            ("bad.exe", "application/octet-stream", 4096),
        ]
        for name, ctype, size in cases:
            await ws.send(env(41, {"filename": name, "size": size, "contentType": ctype}))
            r = await recv(ws, 42)
            print(f"[{name}] success={r.get('success')} err={r.get('errMsg', '')} "
                  f"url={(r.get('fileUrl') or '')[:48]}")


asyncio.run(main())
