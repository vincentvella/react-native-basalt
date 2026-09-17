#!/usr/bin/env bash
#
# Every test this machine can run, in the order CI runs them.
#
# The sequence existed only inside .github/workflows/ci.yml, which meant the
# way to run everything locally was to read a YAML file and retype it -- and
# meant a suite could quietly stop being run by anybody outside CI. This is the
# same list, in one place, and CI's jobs and this script are meant to stay two
# spellings of one thing.
#
# What runs depends on what is built and what is installed, and each step says
# which when it skips. Nothing here builds a host: a test run that silently
# rebuilds is a test run whose failure might be yesterday's binary.
#
#   scripts/test_all.sh              # everything available
#   scripts/test_all.sh --quick      # skip the end-to-end and parity suites
#   BASALT_BUILD_DIR=build-x scripts/test_all.sh
#
# Exits non-zero if any step failed, after running all of them: one broken
# suite hiding the state of the other six is exactly what this is for.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build="${BASALT_BUILD_DIR:-build}"
quick=0
for argument in "$@"; do
  case "$argument" in
    --quick) quick=1 ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's|^#\ \?||'; exit 0 ;;
    *) echo "unknown argument: $argument" >&2; exit 2 ;;
  esac
done

# Which host is built, which is also which platform the suites below run as.
# No machine has more than one -- a host needs its toolkit -- except this
# repository's own development machine, where GTK is installed on macOS; there
# the AppKit one wins, because that is the platform the machine is.
host=""
platform=""
if [[ -x "$build/basalt_appkit" ]]; then
  host="$build/basalt_appkit"; platform="macos"
elif [[ -x "$build/basalt_gtk" ]]; then
  host="$build/basalt_gtk"; platform="linux"
elif [[ -x "$build/basalt_win32.exe" || -x "$build/basalt_win32" ]]; then
  host="$build/basalt_win32"; platform="windows"
fi

failed=()
skipped=()

# Runs one step, prints a banner, and remembers the verdict. Output is left
# alone: a failing suite's own report is the useful part.
step() {
  local name="$1"; shift
  printf '\n\033[1m== %s\033[0m\n' "$name"
  if "$@"; then
    printf '\033[32mok\033[0m  %s\n' "$name"
  else
    printf '\033[31mFAILED\033[0m  %s\n' "$name"
    failed+=("$name")
  fi
}

skip() {
  printf '\n\033[1m== %s\033[0m\n\033[33mskipped\033[0m -- %s\n' "$1" "$2"
  skipped+=("$1")
}

# --- Unit suites -------------------------------------------------------------
#
# Each platform's own, which also carries core's: the tests in
# packages/react-native-basalt/native/tests belong to neither toolkit, so they
# are compiled into every suite rather than into one.
for suite in basalt_gtk_tests basalt_appkit_tests basalt_win32_tests; do
  if [[ -x "$build/$suite" ]]; then
    step "unit: $suite" "$build/$suite"
  elif [[ -x "$build/$suite.exe" ]]; then
    step "unit: $suite" "$build/$suite.exe"
  fi
done

# --- Node suites -------------------------------------------------------------
#
# These need nothing built and cannot answer differently on another platform,
# so CI runs each of them in exactly one job. Here they always run.
if command -v node >/dev/null 2>&1; then
  # `run-linux`, `run-macos` and `run-windows` are one function with three
  # names, so this checks all three wherever it runs.
  step "cli" node --test scripts/test_cli.js
  # react-native-basalt's JavaScript that needs no React Native to run.
  step "platform javascript" node --test scripts/test_platform_js.js
  # A header reaching <cstdint> through windows.h took CI down for twenty-three
  # commits and nobody developing on Windows could see it. See the file.
  step "include hygiene" node scripts/check_includes.js
else
  skip "node suites" "node is not installed"
fi

if [[ $quick -eq 1 ]]; then
  printf '\n--quick: end-to-end and parity suites not run\n'
elif [[ -z "$host" ]]; then
  skip "end-to-end" "no host built in $build"
  skip "cross-host parity" "no host built in $build"
else
  # --- End to end ------------------------------------------------------------
  #
  # Needs the demo bundled for this platform. Built here rather than depended
  # on, because a stale bundle is a suite that passes against yesterday's
  # JavaScript -- which has happened.
  step "bundle the demo" scripts/bundle.sh ../react-native --prod --platform "$platform"
  step "end-to-end" python3 scripts/integration_test.py --platform "$platform"

  # --- Cross-host parity -----------------------------------------------------
  #
  # Every app in js/ through every host that is built, diffed. Needs two hosts,
  # and says so itself when there is only one.
  step "cross-host parity" scripts/compare_all.sh
fi

# --- Verdict -----------------------------------------------------------------
printf '\n\033[1m== summary\033[0m\n'
if [[ ${#skipped[@]} -gt 0 ]]; then
  printf 'skipped: %s\n' "${skipped[*]}"
fi
if [[ ${#failed[@]} -eq 0 ]]; then
  printf '\033[32meverything that ran, passed\033[0m\n'
  exit 0
fi
printf '\033[31mfailed: %s\033[0m\n' "${failed[*]}"
exit 1
