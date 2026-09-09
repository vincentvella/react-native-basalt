#!/usr/bin/env bash
# Builds the demo app into a Metro bundle the host can load.
#
# Usage:  scripts/bundle.sh [/path/to/react-native] [--dev|--prod] [--platform P]
#
# Built for the `linux` platform: packages/react-native-linux supplies the
# Platform module and the Metro configuration that makes Metro resolve it, so
# an app sees Platform.OS === 'linux' and can use .linux.js files.
#
# --platform android still works, and is what this built before the platform
# package existed. Useful for telling apart "broken on this platform" from
# "broken everywhere".

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV=true
PLATFORM=linux

args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --dev) DEV=true; shift ;;
    --prod) DEV=false; shift ;;
    --platform) PLATFORM="$2"; shift 2 ;;
    *) args+=("$1"); shift ;;
  esac
done

RN_DIR="${args[0]:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$HOME/Workspace/react-native"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass the React Native checkout path, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

[ -x "$RN_DIR/node_modules/.bin/metro" ] || {
  echo "error: no Metro in $RN_DIR/node_modules; run scripts/bootstrap.sh first" >&2
  exit 1
}

OUT="$REPO_ROOT/build/main.jsbundle"
mkdir -p "$REPO_ROOT/build"

echo "==> bundling js/index.js (platform=$PLATFORM, dev=$DEV) against $RN_DIR"
# metro appends .js to --out.
# The entry is resolved against projectRoot (js/), so it is named relative to
# that, not to this script's working directory.
RN_DIR="$RN_DIR" "$RN_DIR/node_modules/.bin/metro" build index.js \
  --platform "$PLATFORM" \
  --dev "$DEV" \
  --out "$OUT" \
  --config "$REPO_ROOT/js/metro.config.js"

echo "==> wrote $OUT.js"
