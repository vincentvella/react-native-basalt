#!/usr/bin/env bash
# Starts Metro for the demo app. See scripts/metro.js, which is where the work
# is: this wrapper is here so that the documented command keeps working.
#
# Usage:  scripts/metro.sh [/path/to/react-native] [--port 8081]
#
# Then, in another terminal:
#
#   BASALT_DEV=1 ./build/basalt_gtk
#
# The Node version exists because Windows has to start a packager too, and a
# shell script is not a way to do that. Rather than two implementations that
# resolve RN_DIR differently, there is one and this delegates to it.

set -euo pipefail

exec node "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/metro.js" "$@"
