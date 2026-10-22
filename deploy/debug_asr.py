"""排查 faster-whisper 与 PyAV 的兼容性（metadata_errors）。"""
import traceback

import av
import faster_whisper

print("av:", av.__version__)
print("faster_whisper:", faster_whisper.__version__)

print("--- av.open kwargs 探测 ---")
try:
    c = av.open("/tmp/t.wav", metadata_errors="ignore")
    print("metadata_errors ok")
    c.close()
except Exception as exc:
    print("metadata_errors 失败:", type(exc).__name__, exc)

try:
    c = av.open("/tmp/t.wav")
    print("plain open ok")
    c.close()
except Exception as exc:
    print("plain open 失败:", type(exc).__name__, exc)

print("--- decode_audio ---")
try:
    from faster_whisper.audio import decode_audio
    samples = decode_audio("/tmp/t.wav")
    print("decode ok, samples:", len(samples))
except Exception:
    traceback.print_exc()

print("--- whisper 转写 ---")
try:
    from faster_whisper import WhisperModel
    m = WhisperModel("/opt/xiaojietong/asr-model", device="cpu", compute_type="int8")
    segments, info = m.transcribe("/tmp/t.wav", language="zh")
    text = "".join(s.text for s in segments)
    print("transcribe ok:", repr(text[:80]), "lang:", info.language)
except Exception:
    traceback.print_exc()
