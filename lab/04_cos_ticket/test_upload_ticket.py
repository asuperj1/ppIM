"""临时联调脚本：验证 UPLOAD_TICKET_REQ(41) 与文件消息校验。

用法（先本地起服务）:
    python lab/04_cos_ticket/test_upload_ticket.py
"""
import asyncio
import json
import uuid

import websockets

URL = "ws://127.0.0.1:18081/ws"

LOGIN_REQ = 1
LOGIN_RSP = 2
CHAT_PRIVATE = 3
REGISTER_REQ = 5
REGISTER_RSP = 6
HISTORY_REQ = 9
HISTORY_RSP = 10
ERROR_PUSH = 13
CHAT_ACK = 14
FRIEND_REQUEST_REQ = 19
FRIEND_REQUEST_PUSH = 21
FRIEND_REQUEST_HANDLE_REQ = 24
FRIEND_REQUEST_HANDLE_RSP = 25
GROUP_CREATE_REQ = 27
GROUP_CREATE_RSP = 28
CHAT_GROUP = 36
CHAT_GROUP_ACK = 37
GROUP_HISTORY_REQ = 38
GROUP_HISTORY_RSP = 39
UPLOAD_REQ = 41
UPLOAD_RSP = 42


def envelope(msg_type, body):
    return json.dumps({"type": msg_type, "body": json.dumps(body)})


async def recv_until(ws, wanted, timeout=5.0, predicate=None):
    """收到指定 type（且满足 predicate）就返回解析后的 body，其它推送丢弃。"""
    deadline = asyncio.get_event_loop().time() + timeout
    while True:
        left = deadline - asyncio.get_event_loop().time()
        if left <= 0:
            raise TimeoutError(f"等不到 type={wanted}")
        raw = await asyncio.wait_for(ws.recv(), timeout=left)
        msg = json.loads(raw)
        if msg.get("type") != wanted:
            continue
        body = json.loads(msg.get("body") or "{}")
        if predicate is None or predicate(body):
            return body


async def signup(ws, tag):
    username = f"cos_{tag}_" + uuid.uuid4().hex[:8]
    password = "test123456"
    await ws.send(envelope(REGISTER_REQ, {
        "username": username,
        "password": password,
        "nickname": "联调" + tag,
    }))
    reg = await recv_until(ws, REGISTER_RSP)
    print(f"[register {tag}]", reg.get("success"), reg.get("errMsg"), reg.get("ppId"))

    await ws.send(envelope(LOGIN_REQ, {"username": username, "password": password}))
    login = await recv_until(ws, LOGIN_RSP)
    print(f"[login {tag}]", login.get("success"), login.get("errMsg"))
    return login


async def main():
    async with websockets.connect(URL, max_size=8 * 1024 * 1024) as alice, \
            websockets.connect(URL, max_size=8 * 1024 * 1024) as bob:
        a = await signup(alice, "a")
        b = await signup(bob, "b")
        if not a.get("success") or not b.get("success"):
            return
        bob_uid = b["uid"]
        alice_uid = a["uid"]

        # 0) 先加好友，否则私聊会被 403 拦住
        await alice.send(envelope(FRIEND_REQUEST_REQ, {"toUid": bob_uid, "message": "联调"}))
        push_req = await recv_until(bob, FRIEND_REQUEST_PUSH)
        request_id = (push_req.get("request") or {}).get("requestId")
        await bob.send(envelope(FRIEND_REQUEST_HANDLE_REQ, {"requestId": request_id, "accept": True}))
        handled = await recv_until(bob, FRIEND_REQUEST_HANDLE_RSP)
        print("[friend]", request_id, handled.get("success"))

        # 1) 合法票据
        await alice.send(envelope(UPLOAD_REQ, {
            "filename": "shot.png", "size": 148213, "contentType": "image/png",
        }))
        ticket = await recv_until(alice, UPLOAD_RSP)
        print("[ticket ok]", ticket.get("success"), ticket.get("key"))
        file_url = ticket.get("fileUrl")

        # 2) 大小超限
        await alice.send(envelope(UPLOAD_REQ, {
            "filename": "big.zip", "size": 30 * 1024 * 1024,
            "contentType": "application/zip",
        }))
        rsp = await recv_until(alice, UPLOAD_RSP)
        print("[too big]", rsp.get("success"), rsp.get("errMsg"))

        # 3) 非法后缀
        await alice.send(envelope(UPLOAD_REQ, {
            "filename": "run.exe", "size": 1024,
            "contentType": "application/octet-stream",
        }))
        rsp = await recv_until(alice, UPLOAD_RSP)
        print("[bad ext]", rsp.get("success"), rsp.get("errMsg"))

        # 4) 伪造文件域名
        await alice.send(envelope(CHAT_PRIVATE, {
            "toUid": bob_uid, "content": "[图片]", "msgKind": 2,
            "fileUrl": "https://evil.example.com/x.png",
            "fileName": "x.png", "fileSize": 1024,
            "clientMsgId": str(uuid.uuid4()),
        }))
        err = await recv_until(alice, ERROR_PUSH)
        print("[fake host]", err.get("code"), err.get("message"))

        # 5) 正常图片消息：B 应收到带 fileUrl 的推送
        cid = str(uuid.uuid4())
        await alice.send(envelope(CHAT_PRIVATE, {
            "toUid": bob_uid, "content": "[图片]", "msgKind": 2,
            "fileUrl": file_url, "fileName": "shot.png", "fileSize": 148213,
            "clientMsgId": cid,
        }))
        ack = await recv_until(alice, CHAT_ACK)
        print("[ack]", ack.get("msgId"), ack.get("clientMsgId") == cid)

        push = await recv_until(bob, CHAT_PRIVATE)
        print("[push]", push.get("msgKind"), push.get("content"),
              push.get("fileName"), push.get("fileSize"),
              push.get("fileUrl") == file_url, push.get("fromUid") == alice_uid)

        # 6) 历史回填
        await bob.send(envelope(HISTORY_REQ, {"peerUid": alice_uid, "beforeId": 0, "limit": 10}))
        hist = await recv_until(bob, HISTORY_RSP)
        rows = hist.get("messages") or []
        last = rows[-1] if rows else {}
        print("[history]", len(rows), last.get("msgKind"), last.get("fileUrl") == file_url)

        # 7) 群文件消息
        await alice.send(envelope(GROUP_CREATE_REQ, {
            "name": "文件联调群", "memberUids": [bob_uid],
        }))
        created = await recv_until(alice, GROUP_CREATE_RSP)
        gid = (created.get("group") or {}).get("gid")
        print("[group create]", created.get("success"), gid)

        await alice.send(envelope(CHAT_GROUP, {
            "gid": gid, "content": "[文件] doc.pdf", "msgKind": 3,
            "fileUrl": file_url, "fileName": "doc.pdf", "fileSize": 20480,
            "clientMsgId": str(uuid.uuid4()),
        }))
        await recv_until(alice, CHAT_GROUP_ACK)
        gpush = await recv_until(bob, CHAT_GROUP, predicate=lambda body: body.get("msgKind") == 3)
        print("[group push]", gpush.get("msgKind"), gpush.get("content"),
              gpush.get("fileName"), gpush.get("fileUrl") == file_url)

        await bob.send(envelope(GROUP_HISTORY_REQ, {"gid": gid, "beforeId": 0, "limit": 20}))
        ghist = await recv_until(bob, GROUP_HISTORY_RSP)
        grows = ghist.get("messages") or []
        glast = grows[-1] if grows else {}
        print("[group history]", len(grows), glast.get("msgKind"), glast.get("fileUrl") == file_url)


if __name__ == "__main__":
    asyncio.run(main())
