#!/usr/bin/env bash
# 只读检查：ASR 服务依赖与资源
set -uo pipefail
PY=/opt/xiaojietong/app/backend/.venv/bin/python

echo "== python =="
"$PY" -V

echo "== faster_whisper =="
"$PY" - <<'EOF' 2>&1 | tail -3
try:
    import faster_whisper
    print("installed:", getattr(faster_whisper, "__version__", "unknown"))
except Exception as exc:
    print("missing:", type(exc).__name__, exc)
EOF

echo "== 磁盘 =="
df -h /opt | tail -1

echo "== 内存 =="
free -m | sed -n '1,2p'

echo "== 模型缓存 =="
ls -d ~/.cache/huggingface/hub/models--*whisper* 2>/dev/null || echo "无 whisper 模型缓存"
