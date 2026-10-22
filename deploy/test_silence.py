"""验证 ASR 服务的空语音检测（静音 / 底噪）不再返回"优优独播剧场"。"""

import io
import math
import random
import struct
import wave

import httpx


def wav_bytes(seconds=2.0, rate=16000, amplitude=0.0):
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        n = int(seconds * rate)
        if amplitude <= 0:
            frames = b"\x00\x00" * n
        else:
            vals = []
            for _ in range(n):
                s = random.uniform(-amplitude, amplitude)
                vals.append(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)))
            frames = b"".join(vals)
        w.writeframes(frames)
    return buf.getvalue()


def post(url, data, name):
    r = httpx.post(url, files={"file": (name, data)}, data={"language": "zh"}, timeout=120)
    return r.status_code, r.text


def main():
    url = "http://127.0.0.1:9001/transcribe"
    for label, amp in (("纯静音", 0.0), ("底噪 -54dBFS", 0.002), ("较大底噪 -40dBFS", 0.01)):
        code, text = post(url, wav_bytes(2.0, amplitude=amp), "silence.wav")
        print(f"{label}: HTTP {code} -> {text}", flush=True)

    # 正常人声级信号（1kHz 正弦，约 -20dBFS），用来确认没有把真声音误杀
    rate = 16000
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = b"".join(
            struct.pack("<h", int(0.1 * 32767 * math.sin(2 * math.pi * 1000 * i / rate)))
            for i in range(rate * 2)
        )
        w.writeframes(frames)
    code, text = post(url, buf.getvalue(), "tone.wav")
    print(f"1kHz 正弦 -20dBFS: HTTP {code} -> {text}", flush=True)


if __name__ == "__main__":
    main()
