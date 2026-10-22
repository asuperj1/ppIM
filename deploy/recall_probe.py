"""对端视角验证「撤回」推送：登录 vtuser1，观察 CHAT_PRIVATE 与 RECALL_PUSH。

用法：python3 recall_probe.py <seconds>
"""

import base64
import json
import os
import socket
import struct
import sys
import time


def connect(host="127.0.0.1", port=18080):
    s = socket.create_connection((host, port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = ("GET /ws HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
           "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n") % (host, port, key)
    s.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(1)
    return s


def send_text(s, payload):
    data = payload.encode()
    mask = os.urandom(4)
    n = len(data)
    if n < 126:
        header = bytes([0x81, 0x80 | n])
    elif n < 65536:
        header = bytes([0x81, 0x80 | 126]) + struct.pack("!H", n)
    else:
        header = bytes([0x81, 0x80 | 127]) + struct.pack("!Q", n)
    s.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c:
            raise RuntimeError("closed")
        buf += c
    return buf


def read_frame(s):
    head = recv_exact(s, 2)
    opcode = head[0] & 0x0F
    length = head[1] & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(s, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(s, 8))[0]
    return opcode, recv_exact(s, length) if length else b""


def main():
    username = sys.argv[1] if len(sys.argv) > 1 else "vtuser1"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    s = connect()
    s.settimeout(1.0)
    send_text(s, json.dumps({"type": 1, "body": json.dumps({"username": username, "password": "test1234"})}))

    print("logged in as %s, watching for %.0fs" % (username, seconds), flush=True)
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            opcode, payload = read_frame(s)
        except socket.timeout:
            continue
        except RuntimeError:
            print("connection closed", flush=True)
            return
        try:
            env = json.loads(payload.decode())
        except Exception:
            continue
        t = env.get("type")
        body = env.get("body", "")
        if t == 3:
            data = json.loads(body)
            print("CHAT_PRIVATE msg_id=%s content=%r" % (data.get("msgId"), data.get("content")), flush=True)
        elif t == 65:
            print("RECALL_PUSH %s" % body, flush=True)
        elif t == 68:
            print("PAT_PUSH %s" % body, flush=True)
        elif t == 63:
            print("RECALL_REQ %s" % body, flush=True)
        elif t == 26:
            data = json.loads(body)
            for friend in data.get("friends", []):
                if str(friend.get("uid")) == "36":
                    print("FRIEND 36 avatarUrl=%s" % (friend.get("avatarUrl") or "(空)"), flush=True)
    print("done", flush=True)


if __name__ == "__main__":
    main()
