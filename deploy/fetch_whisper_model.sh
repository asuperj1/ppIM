#!/usr/bin/env bash
# 从 ModelScope 拉取 faster-whisper small（CTranslate2 格式）到本地目录，
# 供校捷通 ASR 服务以「本地模型路径」方式加载（服务器无法直连 huggingface）。
set -euo pipefail

DEST=${1:-/opt/xiaojietong/asr-model}
BASE="https://www.modelscope.cn/models/Systran/faster-whisper-small/resolve/master"

mkdir -p "$DEST"
for f in config.json tokenizer.json vocabulary.txt preprocessor_config.json model.bin; do
  if [ -s "$DEST/$f" ]; then
    echo "skip $f ($(du -h "$DEST/$f" | cut -f1))"
    continue
  fi
  echo "downloading $f ..."
  curl -sSL --retry 3 --retry-delay 2 -m 900 -o "$DEST/$f.part" "$BASE/$f"
  mv "$DEST/$f.part" "$DEST/$f"
  echo "  ok $(du -h "$DEST/$f" | cut -f1)"
done

chmod -R a+r "$DEST"
ls -lh "$DEST"
