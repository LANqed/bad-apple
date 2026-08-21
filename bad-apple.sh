#!/usr/bin/env bash
set -euo pipefail

# Local Linux entry point. The online one is install.sh.
exec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/install.sh" "$@"
