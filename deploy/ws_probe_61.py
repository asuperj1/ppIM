import base64
import json
import os
import socket
import struct


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
    s = connect()
    send_text(s, json.dumps({"type": 1, "body": json.dumps({"username": "vtuser1", "password": "test1234"})}))
    for _ in range(2):
        read_frame(s)

    # 发 FAVORITE_REMOVE_REQ(61)，看是否命中 case（期望 type=62）
    send_text(s, json.dumps({"type": 61, "body": json.dumps({"msgId": "1"})}))
    print("sent 61")
    try:
        for _ in range(8):
            op, payload = read_frame(s)
            print("opcode", op, "len", len(payload), "head", payload[:70])
            if payload.startswith(b"{"):
                m = json.loads(payload)
                if m.get("type") in (60, 62):
                    print("MATCHED", m)
                    break
    except Exception as exc:
        print("ERR", type(exc).__name__, exc)
    s.close()


main()
