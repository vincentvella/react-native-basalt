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
# Needs at least two hosts. Bundles are built if missing, for whichever
# platforms are going to run -- inside the distro, for a Linux host in WSL.
#
#   scripts/compare_all.sh
#   BASALT_COMPARE_WSL=Ubuntu-24.04 scripts/compare_all.sh    # from Windows

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
  # js/hover.js is deliberately absent. Its tree depends on where the cursor
  # is: a box under the pointer takes a different background, and a window that
  # opens under someone's mouse is hovered before either host has drawn
  # anything. That is a property of the machine rather than of the platform, so
  # it makes a bad parity fixture -- what checks hover is the end-to-end suite,
  # which drives the pointer itself and runs on both hosts.
  #
  # The real demo, last: the richest app there is, and the one whose tree
  # agreeing means the most.
  "index:BasaltDemo"
)

# Which hosts exist, and therefore which platforms need bundles. Kept in step
# with compare_hosts.sh by asking the same questions of the same places.
platforms=()
[[ -x "$build/basalt_gtk" ]] && platforms+=("linux")
[[ -x "$build/basalt_appkit" ]] && platforms+=("macos")
{ [[ -x "$build/basalt_win32.exe" ]] || [[ -x "$build/basalt_win32" ]]; } && platforms+=("windows")

# A Linux host inside WSL counts as the Linux host when there is no local one.
# See compare_hosts.sh for why this is the only way one machine gets two hosts,
# and for why every path handed to wsl.exe has conversion turned off.
wsl_distro="${BASALT_COMPARE_WSL:-}"
wsl_repo="${BASALT_COMPARE_WSL_REPO:-/root/react-native-basalt}"
wsl_run() {
  MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 wsl.exe -d "$wsl_distro" -u root -e "$@"
}
linux_in_wsl=""
if [[ -n "$wsl_distro" && ! " ${platforms[*]} " =~ " linux " ]]; then
  if wsl_run test -x "$wsl_repo/build/basalt_gtk" 2>/dev/null; then
    platforms+=("linux")
    linux_in_wsl=1
  else
    echo "BASALT_COMPARE_WSL is set but $wsl_repo/build/basalt_gtk is not built in $wsl_distro" >&2
    exit 1
  fi
fi

if [[ ${#platforms[@]} -lt 2 ]]; then
  echo "need at least two hosts built to compare; found ${#platforms[@]} in $build" >&2
  exit 1
fi
echo "hosts: ${platforms[*]}${linux_in_wsl:+ (linux in $wsl_distro)}"
echo

quit_after="${BASALT_COMPARE_QUIT_AFTER_MS:-2800}"
failures=0

for entry in "${APPS[@]}"; do
  name="${entry%%:*}"
  module="${entry##*:}"

  for platform in "${platforms[@]}"; do
    if [[ "$platform" == linux && -n "$linux_in_wsl" ]]; then
      if ! wsl_run test -f "$wsl_repo/build/$name.linux.jsbundle.js"; then
        echo "  building $name.linux.jsbundle.js in $wsl_distro"
        wsl_run bash -lc "cd '$wsl_repo' && scripts/bundle.sh react-native-src \
          --platform linux --entry '$name.js' --out '$name.linux.jsbundle'" >/dev/null 2>&1
      fi
      continue
    fi
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
