#!/usr/bin/env bash
# Starts Metro for the demo app, so the host can load its bundle over http and
# get Fast Refresh.
#
# Usage:  scripts/metro.sh [/path/to/react-native] [--port 8081]
#
# Then, in another terminal:
#
#   BASALT_DEV=1 ./build/basalt_gtk
#
# The host asks for "http://localhost:8081/index.bundle?platform=android&...",
# a URL DevServerHelper builds from ReactInstanceConfig plus the source path.
# Metro must therefore be serving js/ as its project root, which is what the
# --config below arranges.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT=8081

args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    *) args+=("$1"); shift ;;
  esac
done

RN_DIR="${args[0]:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  # react-native-src too, for the reason bundle.sh gives: it is where CI and
  # wsl_setup.sh put the checkout. Without it the two scenarios that start their
  # own Metro -- Fast Refresh and the developer menu -- failed with "metro
  # exited before it started serving" anywhere RN_DIR was not set by hand.
  for candidate in "$REPO_ROOT/../react-native" "$REPO_ROOT/react-native-src" \
                   "$HOME/Workspace/react-native"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass the React Native checkout path, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

[ -d "$RN_DIR/node_modules/react-native" ] || {
  echo "error: $RN_DIR has no installed dependencies; run scripts/bootstrap.sh first" >&2
  exit 1
}

echo "==> Metro on port $PORT, project root $REPO_ROOT/js"
exec env RN_DIR="$RN_DIR" "$RN_DIR/node_modules/.bin/metro" serve \
  --config "$REPO_ROOT/js/metro.config.js" \
  --port "$PORT"
