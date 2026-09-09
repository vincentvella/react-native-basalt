#!/usr/bin/env bash
# Builds the demo app into a Metro bundle the host can load.
#
# Usage:  scripts/bundle.sh [/path/to/react-native] [--dev|--prod]
#
# The bundle is built for the *android* platform on purpose. ReactCxxPlatform's
# PlatformConstantsModule reports PlatformConstantsAndroid, so React Native's JS
# already believes it is running on Android when hosted this way, and Metro must
# resolve the matching .android.js files. A real `linux` platform is a later
# chunk of work: it needs a platform package that supplies its own Platform
# module and component registry.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV=true

args=()
for arg in "$@"; do
  case "$arg" in
    --dev) DEV=true ;;
    --prod) DEV=false ;;
    *) args+=("$arg") ;;
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

[ -d "$RN_DIR/node_modules/react-native" ] || {
  echo "error: $RN_DIR has no installed dependencies; run scripts/bootstrap.sh first" >&2
  exit 1
}

OUT="$REPO_ROOT/build/main.jsbundle"
mkdir -p "$REPO_ROOT/build"

echo "==> bundling js/index.js (dev=$DEV) against $RN_DIR"
# metro appends .js to --out.
# The entry is resolved against projectRoot (js/), so it is named relative to
# that, not to this script's working directory.
RN_DIR="$RN_DIR" "$RN_DIR/node_modules/.bin/metro" build index.js \
  --platform android \
  --dev "$DEV" \
  --out "$OUT" \
  --config "$REPO_ROOT/js/metro.config.js"

echo "==> wrote $OUT.js"
