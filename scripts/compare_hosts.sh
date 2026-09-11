#!/usr/bin/env bash
#
# Runs the same script through both hosts and diffs the view tree each produced.
#
# This is the check the whole architecture is for. The same JavaScript, the same
# Fabric, the same Yoga, and two completely different view layers underneath --
# if the trees differ, one of the platforms is wrong, and which one is a
# question the diff answers before anybody looks at a screenshot.
#
# Needs both toolkits present, so it runs on a Mac with GTK installed rather
# than in CI, where each host only exists on its own side. Usage:
#
#   scripts/compare_hosts.sh [script] [moduleName]
#
# defaulting to the raw-Fabric demo, which is what macOS can currently mount.

set -euo pipefail

script="${1:-js/demo.js}"
module="${2:-}"
quit_after="${RN_COMPARE_QUIT_AFTER_MS:-4000}"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

for host in build/rn_linux_host build/rn_mac_host; do
  if [[ ! -x "$host" ]]; then
    echo "missing $host -- build both hosts first:" >&2
    echo "  cmake --build build" >&2
    exit 1
  fi
done

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

echo "running the GTK host..."
RN_LINUX_DUMP_TREE="$out/gtk.txt" RN_LINUX_QUIT_AFTER_MS="$quit_after" \
  build/rn_linux_host "$script" "$module" >"$out/gtk.log" 2>&1 || true

echo "running the macOS host..."
RN_MAC_DUMP_TREE="$out/mac.txt" RN_MAC_QUIT_AFTER_MS="$quit_after" \
  build/rn_mac_host "$script" "$module" >"$out/mac.log" 2>&1 || true

for side in gtk mac; do
  if [[ ! -s "$out/$side.txt" ]]; then
    echo "the $side host wrote no tree; its log:" >&2
    tail -20 "$out/$side.log" >&2
    exit 1
  fi
done

if diff -u "$out/gtk.txt" "$out/mac.txt"; then
  echo
  echo "identical:"
  sed 's/^/  /' "$out/gtk.txt"
else
  echo >&2
  echo "the two hosts disagree." >&2
  exit 1
fi
