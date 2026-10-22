#!/usr/bin/env bash
# 验证 pp 域下的语音转写链路：pp.xiaojietong.online/voice-api/* -> 校捷通 ASR
set -uo pipefail

echo "== 1) 生成测试音频 =="
python3 /tmp/gen_test_audio.py

echo
echo "== 2) 经 pp 域（Caddy 注入内部令牌） =="
curl -s -m 90 -X POST 'https://pp.xiaojietong.online/voice-api/api/v1/voice/transcribe' \
  -F 'file=@/tmp/t.wav' -F 'language=zh' | head -c 400
echo

echo "== 3) 直连校捷通主站（无令牌，应 401） =="
curl -s -m 30 -o /dev/null -w 'status=%{http_code}\n' -X POST \
  'https://xiaojietong.online/api/v1/voice/transcribe' -F 'file=@/tmp/t.wav'
