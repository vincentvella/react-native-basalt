#!/usr/bin/env bash
# Fetches and builds everything this project needs that is not in the repo:
# vendored third-party sources, Hermes, and React Native's codegen output.
#
# Usage:  scripts/bootstrap.sh /path/to/react-native-checkout
#
# Idempotent: each step is skipped if its output already exists. Pass --force
# to redo everything.

set -euo pipefail

RN_DIR_ARG="${1:-}"
FORCE="${2:-}"
[ "${1:-}" = "--force" ] && { FORCE="--force"; RN_DIR_ARG=""; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Where the vendored dependencies go. Not beside this script: this script now
# ships inside the npm package, and npm deletes and rewrites node_modules, which
# is not a place to leave a Hermes build. An app passes its own directory;
# scripts/bootstrap.sh in this repository passes the repository root.
TP="${RN_LINUX_THIRD_PARTY:-$PWD/third_party}"
mkdir -p "$TP"
TP="$(cd "$TP" && pwd)"

# Only used to guess where a React Native checkout might be, and only when one
# was not named. Two levels up from the package is the repository root in a
# development checkout, and node_modules in an installed one, where the guess
# will simply fail and the path has to be given.
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

# Versions are React Native's own pins. folly/fast_float/nlohmann come from
# packages/react-native/gradle/libs.versions.toml; Hermes from
# ReactAndroid/hermes-engine/build.gradle.kts. Check them after an RN bump.
FOLLY_VERSION="2024.11.18.00"
FAST_FLOAT_VERSION="8.0.0"
NLOHMANN_VERSION="3.11.3"
HERMES_VERSION="250829098.0.0-stable"

# React Native requires ^22.13.0 || ^24.3.0 || >= 26.0.0. Node 25.x is
# excluded and yarn install fails an engine check in a preinstall hook.
NODE_VERSION="24"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

if [ -n "$FORCE" ]; then
  log "--force: removing fetched trees"
  rm -rf "$TP/folly" "$TP/fast_float" "$TP/nlohmann_json" "$TP/hermes" \
         "$TP/hermes-build" "$TP/node"
  # Not the whole codegen dir: its CMakeLists.txt is checked in and replaces
  # the Android-only one the generator emits. Only the generated tree goes.
  rm -rf "$TP/codegen/react" "$TP"/codegen/*.h "$TP"/codegen/*.cpp
fi

# --- locate the React Native checkout ---------------------------------------
RN_DIR="${RN_DIR_ARG:-${RN_DIR:-}}"
if [ -z "$RN_DIR" ]; then
  for candidate in "$REPO_ROOT/../react-native" "$HOME/Workspace/react-native"; do
    [ -d "$candidate/packages/react-native/ReactCommon" ] && { RN_DIR="$candidate"; break; }
  done
fi
[ -n "$RN_DIR" ] || die "pass the React Native checkout path, or set RN_DIR.
  git clone --depth 1 https://github.com/react/react-native"
RN_DIR="$(cd "$RN_DIR" && pwd)"

# Two shapes are accepted. A source checkout, where the package sits under
# packages/react-native, and an installed one, where the directory given *is*
# the package. The second is what an app has, and it very nearly works: React
# Native ships its C++, the codegen script, the Hermes pin and the version table
# in the npm package. The one thing missing is ReactCxxPlatform, which is absent
# from the package's `files` list, and which is the layer this host is built on.
# See plan/13-upstream-reactcxxplatform.md.
if [ -d "$RN_DIR/packages/react-native/ReactCommon" ]; then
  RN_PKG="$RN_DIR/packages/react-native"
  RN_LAYOUT=checkout
elif [ -d "$RN_DIR/ReactCommon" ]; then
  RN_PKG="$RN_DIR"
  RN_LAYOUT=installed
else
  die "not a React Native checkout or package: $RN_DIR"
fi
log "React Native ($RN_LAYOUT): $RN_PKG"

# --- toolchain --------------------------------------------------------------
for tool in cmake ninja clang++ pkg-config curl tar node; do
  command -v "$tool" >/dev/null || die "missing required tool: $tool"
done
pkg-config --exists gtk4 || die "gtk4 development files not found (pkg-config gtk4)"
log "gtk4 $(pkg-config --modversion gtk4), pango $(pkg-config --modversion pango 2>/dev/null || echo '?')"

# mise is optional; without it, the ambient node must satisfy RN's engines.
if command -v mise >/dev/null; then
  NODE_RUN=(mise exec "node@${NODE_VERSION}" --)
  mise ls node 2>/dev/null | grep -q "^node *${NODE_VERSION}\." || {
    log "installing node@${NODE_VERSION} via mise"
    mise install "node@${NODE_VERSION}"
  }
else
  # Empty array; expanded below with the ${arr[@]+"${arr[@]}"} idiom because
  # bash < 4.4 (macOS ships 3.2) treats "${arr[@]}" of an empty array as unbound
  # under set -u.
  NODE_RUN=()
  node_major="$(node -p 'process.versions.node.split(".")[0]')"
  case "$node_major" in
    22|24|26|27|28) : ;;
    *) die "node $node_major is outside RN's supported range (^22.13 || ^24.3 || >=26).
  Install mise, or switch node." ;;
  esac
fi

mkdir -p "$TP"

# --- vendored headers -------------------------------------------------------
if [ ! -d "$TP/folly" ]; then
  log "fetching folly $FOLLY_VERSION"
  curl -fsSL "https://github.com/facebook/folly/archive/v${FOLLY_VERSION}.tar.gz" \
    | tar xz -C "$TP" && mv "$TP/folly-${FOLLY_VERSION}" "$TP/folly"
fi

if [ ! -d "$TP/fast_float" ]; then
  log "fetching fast_float $FAST_FLOAT_VERSION"
  curl -fsSL "https://github.com/fastfloat/fast_float/archive/v${FAST_FLOAT_VERSION}.tar.gz" \
    | tar xz -C "$TP" && mv "$TP/fast_float-${FAST_FLOAT_VERSION}" "$TP/fast_float"
fi

if [ ! -f "$TP/nlohmann_json/include/nlohmann/json.hpp" ]; then
  log "fetching nlohmann_json $NLOHMANN_VERSION"
  mkdir -p "$TP/nlohmann_json/include/nlohmann"
  curl -fsSL -o "$TP/nlohmann_json/include/nlohmann/json.hpp" \
    "https://github.com/nlohmann/json/releases/download/v${NLOHMANN_VERSION}/json.hpp"
fi

# --- Hermes -----------------------------------------------------------------
if [ ! -d "$TP/hermes" ]; then
  log "fetching Hermes $HERMES_VERSION (~200MB)"
  mkdir -p "$TP/hermes"
  curl -fsSL "https://github.com/facebook/hermes/tarball/${HERMES_VERSION}" \
    | tar xz -C "$TP/hermes" --strip-components=1
fi

if ! ls "$TP"/hermes-build/lib/libhermesvm.* >/dev/null 2>&1; then
  log "building Hermes (slow; RN's own host flags)"
  cmake --log-level=ERROR -G Ninja -S "$TP/hermes" -B "$TP/hermes-build" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DJSI_DIR="$RN_PKG/ReactCommon/jsi" \
    -DCMAKE_BUILD_TYPE=Release \
    -DHERMES_ENABLE_DEBUGGER=True \
    -DHERMESVM_HEAP_HV_MODE=HEAP_HV_PREFER32
  # -j is capped deliberately: a full-width build makes laptops unusable and
  # pins the fans for minutes afterwards.
  nice -n 10 cmake --build "$TP/hermes-build" --target hermesvm -j "${BUILD_JOBS:-12}"
fi

# --- yarn -------------------------------------------------------------------
# React Native's codegen shells out to `yarn install` itself, so yarn has to be
# *on PATH*, not merely invocable -- npx is not enough.
#
# Installing it globally is not an option either: on Linux npm's global prefix
# is root-owned (/usr/lib/node_modules) and `npm install -g` fails with EACCES,
# which a bootstrap script has no business needing sudo to avoid. Homebrew's
# node has a user-writable prefix, which is why this only bites off macOS.
#
# So yarn goes under third_party/ like every other fetched dependency.
if ! command -v yarn >/dev/null; then
  if [ ! -x "$TP/node/node_modules/.bin/yarn" ]; then
    log "installing yarn under third_party/node"
    mkdir -p "$TP/node"
    ${NODE_RUN[@]+"${NODE_RUN[@]}"} npm install --prefix "$TP/node" \
      --silent --no-fund --no-audit yarn@1.22.22
  fi
  export PATH="$TP/node/node_modules/.bin:$PATH"
fi
log "yarn $(yarn --version)"

# --- React Native codegen ---------------------------------------------------
# 24 targets need react_codegen_rncore, ReactCxxPlatform's react/runtime among
# them, so this is mandatory for a host that runs JS.
# --- React Native's dependencies --------------------------------------------
# Deliberately not folded into the codegen step below. Codegen needs these, but
# so does Metro when bundling, and the two are cached separately: a checkout
# with a populated third_party/codegen and an empty node_modules skips this
# step and then cannot bundle. That is exactly what CI hit the first time its
# third_party cache was warm.
if [ "$RN_LAYOUT" = checkout ] && { [ ! -d "$RN_DIR/node_modules" ] || [ -z "$(ls -A "$RN_DIR/node_modules" 2>/dev/null)" ]; }; then
  log "installing React Native's monorepo dependencies"
  (cd "$RN_DIR" && nice -n 10 ${NODE_RUN[@]+"${NODE_RUN[@]}"} yarn install --network-timeout 600000)
fi

# Codegen output belongs to one React Native version and to no other: the
# generated spec headers name every feature flag that version has, so building
# 0.87.1 against artifacts generated from `main` fails on the flags `main` added
# since. The directory is shared rather than per-version, so that
# third_party/codegen/CMakeLists.txt stays put and CI's cache key still works;
# a stamp is what makes reuse safe. Switching versions regenerates.
RN_VERSION="$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
  "$RN_PKG/package.json" | head -1)"
CODEGEN_STAMP="$TP/codegen/.react-native-version"

if [ ! -d "$TP/codegen/react" ] || [ "$(cat "$CODEGEN_STAMP" 2>/dev/null)" != "$RN_VERSION" ]; then
  if [ -d "$TP/codegen/react" ]; then
    log "codegen was generated from $(cat "$CODEGEN_STAMP" 2>/dev/null || echo 'an unknown version'), rebuilding for $RN_VERSION"
    rm -rf "$TP/codegen/react" "$TP"/codegen/*.h "$TP"/codegen/*.cpp
  fi
  log "generating codegen artifacts for React Native $RN_VERSION"
  CODEGEN_TMP="$(mktemp -d)"
  (cd "$RN_PKG" && ${NODE_RUN[@]+"${NODE_RUN[@]}"} node scripts/generate-codegen-artifacts.js \
      -p . -t android -o "$CODEGEN_TMP" -s library -f)

  JNI="$CODEGEN_TMP/android/app/build/generated/source/codegen/jni"
  [ -d "$JNI/react" ] || die "codegen produced no react/ tree at $JNI"
  mkdir -p "$TP/codegen"
  cp -R "$JNI/react" "$TP/codegen/"
  cp "$JNI"/*.h "$JNI"/*.cpp "$TP/codegen/" 2>/dev/null || true
  rm -rf "$CODEGEN_TMP"
  echo "$RN_VERSION" > "$CODEGEN_STAMP"
fi

# The CMakeLists that builds those artifacts is ours, not codegen's. Codegen
# writes one too and it links Android-only targets, so it is deliberately not
# copied out with the rest. Installed every time rather than only alongside a
# regeneration, so that a third_party directory from an older version of this
# package still gets the current one.
cp "$HERE/cmake/codegen/CMakeLists.txt" "$TP/codegen/CMakeLists.txt"

log "bootstrap complete, into $TP. Configure with:"
cat <<EOF

  cmake -B build -G Ninja \\
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \\
    -DRN_DIR=$RN_PKG
  nice -n 10 cmake --build build -j \${BUILD_JOBS:-12}

EOF
