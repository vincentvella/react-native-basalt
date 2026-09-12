#!/usr/bin/env bash
#
# Every app in js/, through every host that is built, diffed.
#
# This is the summary that says where the platforms actually stand. Each app is
# tried twice: first demanding identical trees, then ignoring frames. An app
# that only passes the second is one whose layout depends on text measurement,
# where Pango over the system sans, Core Text over San Francisco and DirectWrite
# over Segoe UI cannot agree and never will -- so what is compared there is the
# tree shape, the strings, the colours, the roles and the flags.
#
# Needs at least two hosts built. Bundles are built if missing, for whichever
# platforms are going to run.
#
#   scripts/compare_all.sh

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build="${BASALT_BUILD_DIR:-build}"

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
  "modules:BasaltModules"
  "probe:BasaltProbe"
  # The real demo, last: the richest app there is, and the one whose tree
  # agreeing means the most.
  "index:BasaltDemo"
)

# Which hosts exist, and therefore which platforms need bundles. Kept in step
# with compare_hosts.sh by asking the same question of the same directory.
platforms=()
[[ -x "$build/basalt_gtk" ]] && platforms+=("linux")
[[ -x "$build/basalt_appkit" ]] && platforms+=("macos")
{ [[ -x "$build/basalt_win32.exe" ]] || [[ -x "$build/basalt_win32" ]]; } && platforms+=("windows")

if [[ ${#platforms[@]} -lt 2 ]]; then
  echo "need at least two hosts built to compare; found ${#platforms[@]} in $build" >&2
  exit 1
fi
echo "hosts: ${platforms[*]}"
echo

quit_after="${BASALT_COMPARE_QUIT_AFTER_MS:-2800}"
failures=0

for entry in "${APPS[@]}"; do
  name="${entry%%:*}"
  module="${entry##*:}"

  for platform in "${platforms[@]}"; do
    bundle="$build/$name.$platform.jsbundle.js"
    if [[ ! -f "$bundle" ]]; then
      echo "  building $bundle"
      scripts/bundle.sh --platform "$platform" --entry "$name.js" \
        --out "$name.$platform.jsbundle" --build-dir "$build" >/dev/null 2>&1
    fi
  done

  if BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
     scripts/compare_hosts.sh "$name" "$module" >/dev/null 2>&1; then
    printf "  %-12s identical, frames included\n" "$name"
  elif BASALT_COMPARE_IGNORE_FRAMES=1 BASALT_COMPARE_QUIT_AFTER_MS="$quit_after" \
       scripts/compare_hosts.sh "$name" "$module" >/dev/null 2>&1; then
    printf "  %-12s identical, frames ignored\n" "$name"
  else
    printf "  %-12s DIFFERS\n" "$name"
    failures=$((failures + 1))
  fi
done

echo
if [[ $failures -eq 0 ]]; then
  echo "the ${#platforms[@]} hosts agree on all ${#APPS[@]} apps"
else
  echo "$failures of ${#APPS[@]} disagree; rerun that one through compare_hosts.sh for the diff" >&2
fi
exit $failures
