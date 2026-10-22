"""校验 COS 上的语音文件是否是可解码的有效音频（不依赖浏览器）。

用法：python deploy/verify_voice_file.py <url> [<url> ...]
"""
import sys
import urllib.request

import av


def probe(url):
    print("=" * 70)
    print("URL:", url.split("/")[-1])
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            data = resp.read()
            ctype = resp.headers.get("Content-Type")
    except Exception as exc:
        print("  下载失败:", exc)
        return

    print(f"  大小: {len(data)} 字节, Content-Type: {ctype}")
    print("  文件头:", data[:12].hex(" "))

    import io
    try:
        container = av.open(io.BytesIO(data))
    except Exception as exc:
        print("  ❌ PyAV 无法解析:", exc)
        return

    for stream in container.streams:
        print(f"  流: {stream.type} codec={stream.codec_context.name} "
              f"rate={getattr(stream, 'rate', None)} "
              f"channels={getattr(stream.codec_context, 'channels', None)}")
    if container.duration:
        print(f"  时长: {container.duration / 1e6:.2f} 秒")

    # 真正解码一段，确认不是空壳
    frames = 0
    samples = 0
    try:
        for frame in container.decode(audio=0):
            samples += frame.samples
            frames += 1
            if frames >= 200:
                break
        print(f"  解码成功: {frames} 帧 / {samples} 采样")
    except Exception as exc:
        print("  ❌ 解码失败:", exc)
    container.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("用法：python verify_voice_file.py <url> [<url>...]")
    for u in sys.argv[1:]:
        probe(u)
