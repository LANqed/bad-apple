#!/usr/bin/env bash
set -euo pipefail

# Set BAD_APPLE_BASE_URL to the directory containing the release binaries.
base_url="${BAD_APPLE_BASE_URL:-https://github.com/LANqed/bad-apple/releases/latest/download}"
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
curl -fL --retry 3 --progress-bar "$base_url/$asset" -o "$target"
chmod +x "$target"
exec "$target" "$@"
