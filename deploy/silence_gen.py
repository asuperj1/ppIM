"""生成 2 秒纯静音 WAV，用于验证端到端链路。"""

import sys
import wave

path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/silence.wav"
rate = 16000
with wave.open(path, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(b"\x00\x00" * rate * 2)
print(path)
