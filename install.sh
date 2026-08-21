#!/usr/bin/env bash
set -euo pipefail

# Set BAD_APPLE_BASE_URL to the directory containing the release binaries.
repo="${BAD_APPLE_REPO:-LANqed/bad-apple}"
base_url="${BAD_APPLE_BASE_URL:-https://github.com/$repo/releases/latest/download}"
case "$(uname -m)" in
  x86_64|amd64) asset='bad-apple-linux-amd64' ;;
  aarch64|arm64) asset='bad-apple-linux-arm64' ;;
  armv7l|armv7|armhf) asset='bad-apple-linux-arm32' ;;
  *) echo "不支持的 Linux 架构: $(uname -m)" >&2; exit 1 ;;
esac

command -v curl >/dev/null || { echo '需要 curl' >&2; exit 1; }
command -v mpv >/dev/null || command -v ffplay >/dev/null ||
  echo '警告: 未找到 mpv/ffplay，将降级为 PC Speaker/终端响铃（非原曲）' >&2

target="${BAD_APPLE_TARGET:-$HOME/.local/bin/bad-apple}"
mkdir -p "$(dirname "$target")"

url="$base_url/$asset"
status=0
curl -fL --retry 3 --progress-bar "$url" -o "$target" || status=$?
if [ "$status" -ne 0 ]; then
  rm -f "$target"
  echo "" >&2
  echo "下载失败 (curl 退出码 $status): $url" >&2
  if [ "$status" -eq 22 ]; then
    echo "" >&2
    echo "HTTP 404 通常说明该仓库还没有发布任何 Release，或 Release 里缺少" >&2
    echo "资产 $asset。可以按以下任一方式解决：" >&2
    echo "" >&2
    echo "  1. 在仓库里修改 VERSION 并推送到 main，等 GitHub Actions 发布 Release" >&2
    echo "  2. 用 BAD_APPLE_BASE_URL 指向已有的二进制目录，例如：" >&2
    echo "       BAD_APPLE_BASE_URL=https://example.com/bin bash install.sh" >&2
    echo "  3. 直接从源码编译（无需 Release）：" >&2
    echo "       curl -fsSL https://raw.githubusercontent.com/$repo/main/src/bad_apple_linux.cpp -o /tmp/ba.cpp" >&2
    echo "       g++ -std=c++17 -O2 /tmp/ba.cpp -o \"$target\" && \"$target\"" >&2
  fi
  exit 1
fi

chmod +x "$target"
exec "$target" "$@"
