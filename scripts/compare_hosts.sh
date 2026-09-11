#!/usr/bin/env bash
#
# Runs the same app through both hosts and diffs the view tree each produced.
#
# This is the check the whole architecture is for. The same React, the same
# Fabric, the same Yoga, and two completely different view layers underneath --
# if the trees differ, one of the platforms is wrong, and which one is a
# question the diff answers before anybody looks at a screenshot.
#
# It checks two things, because they fail differently. The trees have to match,
# and each host has to have got the platform it asked for: a bundle built for
# the wrong platform can still render correctly and be wrong about everything
# `Platform.OS` guards.
#
# Text is the exception, and BASALT_COMPARE_IGNORE_FRAMES=1 is how to say so.
# Pango over the system sans and Core Text over San Francisco are different
# shapers over different fonts: the same paragraph is a few points taller on one
# than the other, and every frame below it shifts. Demanding equality there
# would mean the check could never be turned on for text at all. With frames
# ignored what is still compared is everything that must match -- the tree
# shape, the strings, the colours, the clip and opacity flags -- which is where
# a real bug would show up.
#
# Needs both toolkits present, so it runs on a Mac with GTK installed rather
# than in CI, where each host only exists on its own side.
#
# Usage:
#
#   scripts/compare_hosts.sh                        # the views-only React app
#   scripts/compare_hosts.sh <linux-bundle> <macos-bundle> [moduleName]
#
# A React app needs a bundle per platform, because the platform is baked in at
# bundle time. A raw-Fabric script does not, and the same file can be passed
# twice:
#
#   scripts/compare_hosts.sh js/demo.js js/demo.js ''

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

linux_bundle="${1:-build/views.linux.jsbundle.js}"
mac_bundle="${2:-build/views.macos.jsbundle.js}"
module="${3-BasaltViews}"
quit_after="${BASALT_COMPARE_QUIT_AFTER_MS:-4000}"

# Taps, in surface-root coordinates, forwarded to both hosts. They read the same
# variable, which is one of the things one env-var prefix bought. Each tap is a
# second apart and the first is at 1500ms, so a run with taps needs a longer
# quit than the default.
taps="${BASALT_COMPARE_TAP:-}"

for host in build/basalt_gtk build/basalt_appkit; do
  if [[ ! -x "$host" ]]; then
    echo "missing $host -- build both hosts first:" >&2
    echo "  cmake --build build" >&2
    exit 1
  fi
done

for bundle in "$linux_bundle" "$mac_bundle"; do
  if [[ ! -f "$bundle" ]]; then
    echo "missing $bundle -- build it with:" >&2
    echo "  scripts/bundle.sh --platform <p> --entry views.js --out views.<p>.jsbundle" >&2
    exit 1
  fi
done

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

echo "running the GTK host..."
BASALT_TEST_TAP="$taps" BASALT_DUMP_TREE="$out/linux.txt" BASALT_QUIT_AFTER_MS="$quit_after" \
  build/basalt_gtk "$linux_bundle" "$module" >"$out/linux.log" 2>&1 || true

echo "running the macOS host..."
BASALT_TEST_TAP="$taps" BASALT_DUMP_TREE="$out/macos.txt" BASALT_QUIT_AFTER_MS="$quit_after" \
  build/basalt_appkit "$mac_bundle" "$module" >"$out/macos.log" 2>&1 || true

status=0

for side in linux macos; do
  if [[ ! -s "$out/$side.txt" ]]; then
    echo "the $side host wrote no tree; its log:" >&2
    tail -20 "$out/$side.log" >&2
    exit 1
  fi
done

# Each host should have got the platform its bundle was built for. Only checked
# when the app says so -- js/views.js logs it, a raw-Fabric script does not.
for side in linux macos; do
  if grep -q 'Platform.OS is' "$out/$side.log"; then
    reported="$(sed -n 's/.*Platform\.OS is \([a-z]*\).*/\1/p' "$out/$side.log" | head -1)"
    if [[ "$reported" != "$side" ]]; then
      echo "the $side host reports Platform.OS === '$reported'" >&2
      status=1
    else
      echo "  $side: Platform.OS is $reported"
    fi
  fi
done

# Frames stripped, when asked. Deliberately a whole-line substitution rather
# than a tolerance: "close enough" would need a number nobody can justify, and
# the sizes that differ are the ones this cannot check anyway.
if [[ -n "${BASALT_COMPARE_IGNORE_FRAMES:-}" ]]; then
  for side in linux macos; do
    sed -E 's/ frame=\([^)]*\)//' "$out/$side.txt" > "$out/$side.stripped"
    mv "$out/$side.stripped" "$out/$side.txt"
  done
  echo "  (frames ignored)"
fi

if diff -u "$out/linux.txt" "$out/macos.txt"; then
  echo
  echo "the two hosts produced the same tree:"
  sed 's/^/  /' "$out/linux.txt"
else
  echo >&2
  echo "the two hosts disagree." >&2
  status=1
fi

exit $status
