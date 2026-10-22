#!/usr/bin/env bash
# =============================================================================
#  deploy/build-linux.sh
#  在 Linux 服务器上构建 pp。
#
#  使用 Ubuntu / Debian 系统包（不访问 GitHub、不编译 vcpkg 依赖）：
#    protobuf / spdlog / fmt / sqlite3 / asio
#
#  首次构建约 1~3 分钟。
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if command -v apt-get >/dev/null 2>&1; then
    echo "==> 安装系统依赖"
    sudo apt-get update -y
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential cmake git pkg-config \
        libprotobuf-dev protobuf-compiler \
        libspdlog-dev libfmt-dev libsqlite3-dev libasio-dev
else
    echo "非 Debian 系系统，请自行安装 protobuf / spdlog / fmt / sqlite3 / asio 开发包" >&2
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "缺少 cmake，请先安装 cmake（需要 3.21+）" >&2
    exit 1
fi

echo "==> 配置"
cmake --preset linux

echo "==> 构建"
cmake --build --preset linux-release --parallel "$(nproc)"

echo
echo "构建完成：$ROOT_DIR/build-linux/bin/im_server"
echo "启动示例："
echo "  ./build-linux/bin/im_server --http-port 18080 --tcp-port 19000 \\"
echo "      --web-root $ROOT_DIR/web --db $ROOT_DIR/data/im.db"
