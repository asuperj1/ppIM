#!/usr/bin/env bash
# 配置 pp → 校捷通语音转写的内部令牌（不回显明文）
# 用法：在服务器上 sudo bash voice-token-setup.sh
set -euo pipefail

ENV_FILE=/opt/xiaojietong/shared/server.env
DROPIN_DIR=/etc/systemd/system/caddy.service.d
DROPIN=$DROPIN_DIR/voice-token.conf

# 1) 已有 XJT_INTERNAL_TOKEN 且非空则复用，否则生成新的
CUR=$(grep -E '^XJT_INTERNAL_TOKEN=' "$ENV_FILE" | head -n1 | cut -d= -f2- || true)
if [ -z "${CUR:-}" ]; then
  CUR=$(python3 -c 'import secrets;print(secrets.token_urlsafe(32))')
  # 移除旧的空行配置再追加，避免重复键
  grep -vE '^XJT_INTERNAL_TOKEN=' "$ENV_FILE" > "$ENV_FILE.tmp" || true
  printf 'XJT_INTERNAL_TOKEN=%s\n' "$CUR" >> "$ENV_FILE.tmp"
  cat "$ENV_FILE.tmp" > "$ENV_FILE"
  rm -f "$ENV_FILE.tmp"
  chown xjt:xjt "$ENV_FILE"
  chmod 600 "$ENV_FILE"
  echo "server.env: XJT_INTERNAL_TOKEN 已生成（长度 ${#CUR}）"
else
  echo "server.env: 复用已有 XJT_INTERNAL_TOKEN（长度 ${#CUR}）"
fi

# 2) 写入 Caddy 的 systemd drop-in（Caddyfile 用 {env.XJT_VOICE_TOKEN} 引用）
mkdir -p "$DROPIN_DIR"
printf '[Service]\nEnvironment="XJT_VOICE_TOKEN=%s"\n' "$CUR" > "$DROPIN"
chmod 600 "$DROPIN"

systemctl daemon-reload
systemctl restart xiaojietong-api
systemctl restart caddy
sleep 3
echo "xiaojietong-api=$(systemctl is-active xiaojietong-api) caddy=$(systemctl is-active caddy)"
