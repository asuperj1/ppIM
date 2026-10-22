"""裸 socket 抓 WS 帧：定位服务端在收到收藏列表请求后发出的坏帧。"""
import base64
import json
import os
import socket
import struct
import sys


def connect(host="127.0.0.1", port=18080):
    s = socket.create_connection((host, port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET /ws HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    s.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = s.recv(1)
        if not chunk:
            raise RuntimeError("handshake closed")
        buf += chunk
    print("HANDSHAKE:", buf.split(b"\r\n")[0].decode(errors="replace"))
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
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise RuntimeError(f"closed after {len(buf)}/{n} bytes")
        buf += chunk
    return buf


def read_frame(s):
    head = recv_exact(s, 2)
    opcode = head[0] & 0x0F
    fin = bool(head[0] & 0x80)
    length = head[1] & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(s, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(s, 8))[0]
    payload = recv_exact(s, length) if length else b""
    return opcode, fin, payload


def main():
    s = connect()
    send_text(s, json.dumps({"type": 1, "body": json.dumps({"username": "vtuser1", "password": "test1234"})}))

    for _ in range(2):
        op, fin, payload = read_frame(s)
        print(f"frame opcode={op} fin={fin} len={len(payload)} head={payload[:60]!r}")

    send_text(s, json.dumps({"type": 59, "body": "{}"}))
    print("sent FAVORITE_LIST_REQ(59)")

    try:
        for _ in range(8):
            op, fin, payload = read_frame(s)
            print(f"frame opcode={op} fin={fin} len={len(payload)} head={payload[:80]!r}")
            if payload.startswith(b"{"):
                try:
                    m = json.loads(payload)
                    if m.get("type") == 60:
                        print("GOT FAVORITE_LIST_RSP")
                        break
                except Exception:
                    pass
    except Exception as exc:
        print("READ ERROR:", type(exc).__name__, exc)

    s.close()


if __name__ == "__main__":
    sys.exit(main())
