"""为语音功能验证准备两个互为好友的测试账号（vtuser1 / vtuser2）。

用法：python lab/11_voice/seed_friends.py
"""
import asyncio
import json
import sys

import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "wss://pp.xiaojietong.online/ws"
PWD = "test1234"


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


async def ensure_account(name, nickname):
    """注册（已存在则忽略），返回 (uid, ppId)。"""
    async with websockets.connect(URL) as ws:
        await ws.send(env(5, {"username": name, "password": PWD, "nickname": nickname}))
        rsp = await recv(ws, 6)
        if not rsp.get("success"):
            print(f"[{name}] 已存在（{rsp.get('errMsg')}），改用登录")
        else:
            print(f"[{name}] 注册成功 ppId={rsp.get('ppId')} uid={rsp.get('uid')}")
            return int(rsp.get("uid") or 0), rsp.get("ppId")

    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": name, "password": PWD}))
        rsp = await recv(ws, 2)
        if not rsp.get("success"):
            raise SystemExit(f"[{name}] 登录失败：{rsp.get('errMsg')}")
        print(f"[{name}] 登录成功 uid={rsp.get('uid')} ppId={rsp.get('ppId')}")
        return int(rsp.get("uid") or 0), rsp.get("ppId")


async def main():
    uid1, pp1 = await ensure_account("vtuser1", "语音甲")
    uid2, pp2 = await ensure_account("vtuser2", "语音乙")
    print("账号：", uid1, pp1, "|", uid2, pp2)

    # vtuser2 向 vtuser1 发好友申请
    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": "vtuser2", "password": PWD}))
        await recv(ws, 2)
        await ws.send(env(19, {"toUid": uid1, "message": "我是语音乙"}))
        rsp = await recv(ws, 20)
        print("[申请]", rsp.get("success"), rsp.get("errMsg", ""))

    # vtuser1 拉申请列表并同意
    async with websockets.connect(URL) as ws:
        await ws.send(env(1, {"username": "vtuser1", "password": PWD}))
        await recv(ws, 2)
        await ws.send(env(22, {}))
        lst = await recv(ws, 23)
        pending = [r for r in (lst.get("requests") or []) if int(r.get("fromUid") or 0) == uid2]
        if not pending:
            print("[申请列表] 没有来自 vtuser2 的待处理申请（可能已是好友）")
        else:
            req = pending[0]
            await ws.send(env(24, {"requestId": int(req["requestId"]), "accept": True}))
            rsp = await recv(ws, 25)
            print("[同意]", rsp.get("success"), rsp.get("errMsg", ""))

    # 互发一条消息，确保会话列表里有对方
    for name, to_uid, text in (("vtuser2", uid1, "语音测试 hi"), ("vtuser1", uid2, "语音测试 hi")):
        async with websockets.connect(URL) as ws:
            await ws.send(env(1, {"username": name, "password": PWD}))
            await recv(ws, 2)
            await ws.send(env(3, {"toUid": to_uid, "content": text,
                                  "clientMsgId": f"seed-{name}-{to_uid}"}))
            try:
                ack = await recv(ws, 14, timeout=6)
                print(f"[{name} 发消息]", ack.get("success"), ack.get("errMsg", ""))
            except asyncio.TimeoutError:
                print(f"[{name} 发消息] 无 ack")

    print("完成")


asyncio.run(main())
