#!/usr/bin/env bash
#
# Every app in js/, through both hosts, diffed.
#
# This is the summary that says where the two platforms actually stand. Each app
# is tried twice: first demanding identical trees, then ignoring frames. An app
# that only passes the second is one whose layout depends on text measurement,
# where Pango over the system sans and Core Text over San Francisco cannot
# agree and never will -- so what is compared there is the tree shape, the
# strings, the colours, the roles and the flags.
#
# Needs both toolkits and both hosts built. Bundles are built if missing.
#
#   scripts/compare_all.sh

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# app entry : registered module name
APPS=(
  "views:BasaltViews"
  "press:BasaltPress"
  "scroll:BasaltScroll"
  "image:BasaltImage"
  "text:BasaltText"
  "a11y:BasaltA11y"
  "input:BasaltInput"
  "appearance:BasaltAppearance"
  "blob:BasaltBlob"
  # The real demo, last: the richest app there is, and the one whose tree
  # agreeing means the most.
  "index:BasaltDemo"
)

quit_after="${BASALT_COMPARE_QUIT_AFTER_MS:-2800}"
failures=0

for entry in "${APPS[@]}"; do
  name="${entry%%:*}"
  module="${entry##*:}"

  for platform in linux macos; do
    bundle="build/$name.$platform.jsbundle.js"
    if [[ ! -f "$bundle" ]]; then
      echo "  building $bundle"
      scripts/bundle.sh --platform "$platform" --entry "$name.js" \
        --out "$name.$platform.jsbundle" >/dev/null 2>&1
    fi
  done

  linux_bundle="build/$name.linux.jsbundle.js"
  mac_bundle="build/$name.macos.jsbundle.js"

  if BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
     scripts/compare_hosts.sh "$linux_bundle" "$mac_bundle" "$module" >/dev/null 2>&1; then
    printf "  %-8s identical, frames included\n" "$name"
  elif BASALT_COMPARE_IGNORE_FRAMES=1 BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
       scripts/compare_hosts.sh "$linux_bundle" "$mac_bundle" "$module" >/dev/null 2>&1; then
    printf "  %-8s identical, frames ignored\n" "$name"
  else
    printf "  %-8s DIFFERS\n" "$name"
    failures=$((failures + 1))
  fi
done

echo
if [[ $failures -eq 0 ]]; then
  echo "the two hosts agree on all ${#APPS[@]} apps"
else
  echo "$failures of ${#APPS[@]} disagree; rerun that one through compare_hosts.sh for the diff" >&2
fi
exit $failures
