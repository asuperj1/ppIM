"""生成 4 秒 440Hz 测试音频（用于验证语音转写链路是否打通，不用于识别准确率测试）。"""
import math
import struct
import wave

path = "/tmp/t.wav"
rate = 16000
seconds = 4
w = wave.open(path, "wb")
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(rate)
frames = bytearray()
for i in range(rate * seconds):
    frames += struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / rate)))
w.writeframes(bytes(frames))
w.close()
print(path, len(frames))
