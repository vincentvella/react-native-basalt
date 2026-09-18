#!/usr/bin/env bash
# Compiles the JavaScript packages into their `dist/`.
#
# Usage:  scripts/build_ts.sh [/path/to/react-native] [--watch]
#
# TypeScript comes from the React Native checkout's node_modules, which is where
# this repository borrows every node tool from -- see scripts/bundle.sh, which
# finds metro the same way. Nothing here has node_modules of its own, and that
# is deliberate: see js/package.json.
#
# Everything that bundles needs this to have run first, because `main` points
# into `dist/`. scripts/bundle.sh runs it for you.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

args=()
WATCH=false
for arg in "$@"; do
  case "$arg" in
    --watch) WATCH=true ;;
    *) args+=("$arg") ;;
  esac
done

RN_DIR="${args[0]:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$REPO_ROOT/react-native-src"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass the React Native checkout path, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

TSC="$RN_DIR/node_modules/.bin/tsc"
[ -x "$TSC" ] || {
  echo "error: no tsc at $TSC" >&2
  echo "       run yarn in the React Native checkout first" >&2
  exit 1
}

PACKAGES=("$REPO_ROOT/packages/react-native-basalt")

for package in "${PACKAGES[@]}"; do
  [ -f "$package/tsconfig.json" ] || continue
  echo "==> building $(basename "$package")"
  if $WATCH; then
    "$TSC" --build "$package" --watch
  else
    "$TSC" --build "$package"
  fi

  # The overrides are copied rather than compiled. They are Metro's, not node's:
  # Metro resolves them by absolute path out of OVERRIDE_DIR and by platform
  # extension (`./overrides/Platform` -> `Platform.linux.js`), and three of them
  # carry the Flow annotations of the React Native files they shadow. Compiling
  # them would rename nothing and break both. See the tsconfig.
  if [ -d "$package/src/overrides" ]; then
    mkdir -p "$package/dist/src/overrides"
    cp "$package/src/overrides/"*.js "$package/dist/src/overrides/"
  fi
done

echo "==> built"
