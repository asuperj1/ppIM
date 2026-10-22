import base64
import json
import os
import socket
import struct
import time


def connect(host="127.0.0.1", port=18080):
    s = socket.create_connection((host, port), timeout=8)
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


def main():
    s = connect()
    send_text(s, json.dumps({"type": 1, "body": json.dumps({"username": "vtuser1", "password": "test1234"})}))
    time.sleep(1.2)   # 让登录推送先流完
    send_text(s, json.dumps({"type": 61, "body": json.dumps({"msgId": "1"})}))
    time.sleep(1.5)

    s.settimeout(2)
    raw = b""
    try:
        while len(raw) < 4000:
            chunk = s.recv(4096)
            if not chunk:
                break
            raw += chunk
    except socket.timeout:
        pass

    print("RAW BYTES:", len(raw))
    # 逐帧解析，打印 opcode/长度/内容
    i = 0
    while i + 2 <= len(raw):
        b0 = raw[i]
        b1 = raw[i + 1]
        opcode = b0 & 0x0F
        fin = bool(b0 & 0x80)
        length = b1 & 0x7F
        i += 2
        if length == 126:
            length = struct.unpack("!H", raw[i:i + 2])[0]
            i += 2
        elif length == 127:
            length = struct.unpack("!Q", raw[i:i + 8])[0]
            i += 8
        payload = raw[i:i + length]
        i += length
        print(f"opcode={opcode} fin={fin} len={length} :: {payload[:120]!r}")
        if opcode not in (1, 2, 8, 9, 10):
            print("  ^^^ ILLEGAL OPCODE, remaining hex:", raw[max(0, i - length - 4):i + 20].hex(" "))
            break
    s.close()


main()
