#!/usr/bin/env bash
# Makes this directory look enough like an installed app for React Native's CLI.
#
# The CLI finds a project's React Native, and its platform plugins, by walking
# node_modules. This example is not installed from npm -- there is nothing to
# install yet -- so the two entries it needs are linked in by hand. That is the
# whole trick, and it is also a fair rehearsal of what `npm link` would do.
#
# Usage:  examples/demo/setup.sh [/path/to/react-native]

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

RN_DIR="${1:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$REPO_ROOT/../react-native-0.87"; do
    [ -d "$candidate/node_modules/react-native" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || { echo "error: pass a React Native checkout, or set RN_DIR" >&2; exit 1; }
RN_DIR="$(cd "$RN_DIR" && pwd)"

[ -d "$RN_DIR/node_modules/react-native" ] || {
  echo "error: $RN_DIR has no installed dependencies; run scripts/bootstrap.sh first" >&2
  exit 1
}

mkdir -p "$HERE/node_modules"
ln -sfn "$RN_DIR/node_modules/react-native" "$HERE/node_modules/react-native"
ln -sfn "$REPO_ROOT/packages/react-native-linux" "$HERE/node_modules/react-native-linux"

# react-native's cli.js refuses to run unless it finds this in the *project's*
# node_modules, so linking react-native alone is not enough.
mkdir -p "$HERE/node_modules/@react-native-community"
ln -sfn "$RN_DIR/node_modules/@react-native-community/cli" \
  "$HERE/node_modules/@react-native-community/cli"

echo "==> linked react-native from $RN_DIR"
echo "==> linked react-native-linux from $REPO_ROOT/packages"
echo
echo "Now, from $HERE:"
echo "  RN_DIR=$RN_DIR node node_modules/react-native/cli.js run-linux"
