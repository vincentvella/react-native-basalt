# Handoff

Picking this project up on another machine, specifically an Apple Silicon Mac.

## Where things stand

| Phase | Deliverable | State |
|---|---|---|
| 0 | GTK view layer, mounting manager | done |
| 1 | Real Fabric mutations → GTK widgets, no JS | done, verified on screen |
| 2 | RN's full C++ core + Hermes + codegen building | **done** |
| 2 | `main.cpp`: construct `ReactHost`, run a surface | **next** |
| 3 | Metro bundle, flexbox, Fast Refresh | |
| 4+ | Pango text, images, input, AT-SPI | see `docs/ARCHITECTURE.md` |

87 React Native targets build (ReactCommon + ReactCxxPlatform, including
`ReactHost`), plus Hermes and codegen. `mount_harness` drives real
`ShadowViewMutation`s through the real mounting manager into real widgets.

Nothing has yet run JS. `main.cpp` does not exist.

## Setup

```bash
git clone https://github.com/vincentvella/react-native-linux
cd react-native-linux
git clone --depth 1 https://github.com/react/react-native ../react-native
scripts/bootstrap.sh ../react-native
```

Already have a React Native checkout? Skip that clone and point bootstrap at
it (`scripts/bootstrap.sh /path/to/react-native`). Two caveats: bootstrap runs
`yarn install` there if `node_modules` is empty, which rewrites its
`yarn.lock`; and everything here is built against RN `main`, so an older tagged
release will likely need adjustment -- `ReactCxxPlatform` carries no API
stability guarantee.

`bootstrap.sh` fetches folly/fast_float/nlohmann_json, builds Hermes, and runs
React Native's codegen. It is idempotent; `--force` redoes everything. It caps
build parallelism at `BUILD_JOBS` (default 12) deliberately -- a full-width
build makes a laptop unusable and pins the fans well past the end of the build.

Then:

```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DRN_DIR=../react-native/packages/react-native
nice -n 10 cmake --build build -j 12
./build/mount_harness
```

## What is untested on macOS

Everything below is reasoned from the code, **not** verified -- the project has
only ever been built on Linux. Expect some of it to be wrong.

### Dependencies

```bash
brew install gtk4 pango glib glog boost fmt double-conversion gflags \
             openssl cmake ninja pkg-config
```

CMake does not reliably search Homebrew's prefix on Apple Silicon. The
`find_library`/`find_path` calls in `cmake/ThirdParty.cmake` (glog, boost, fmt,
double-conversion) will likely need:

```bash
cmake -B build ... -DCMAKE_PREFIX_PATH="$(brew --prefix)"
```

### Things that should already be right

- **`-latomic`** is guarded by `if(UNIX AND NOT APPLE)` in
  `cmake/ReactNativeCore.cmake`. macOS neither ships nor needs libatomic, and
  Fantom's `CMakeLists.txt` carries the same guard. No change expected.
- **`GLOG_USE_GLOG_EXPORT`** is needed just the same: Homebrew's glog is 0.7.x,
  and RN pins 0.3.5, which predates the guard.
- **Apple clang** should be fine. RN is a clang codebase -- that is precisely
  why this project does not build with GCC.
- **Hermes** builds on macOS; it is the iOS engine. `HERMESVM_HEAP_HV_MODE=
  HEAP_HV_PREFER32` is what RN passes for host builds on every platform.

### Things that may well break

- **GTK4 on macOS** uses the quartz backend, which is far less exercised than
  the Wayland/X11 ones. `RnLayout` and `RnView` use only portable GTK4
  (`GtkLayoutManager`, `gtk_snapshot_append_color`, `gtk_widget_allocate`), so
  they *should* work, but this is the least certain part.
- **`GtkAnimationChoreographer`** depends on `gtk_widget_get_frame_clock` and
  `gdk_frame_clock_begin_updating`. Both exist on the macOS backend; whether the
  frame clock ticks with the same fidelity is unverified.
- **Codegen** shells out through `mise exec node@24`. If mise is not installed,
  `bootstrap.sh` falls back to the ambient node and checks its major version
  against RN's engines (`^22.13 || ^24.3 || >= 26.0.0` -- note that **Node 25.x
  is excluded** and fails an engine check inside a preinstall hook).

### Developer tooling that is Linux-only

The screenshot workflow used to verify rendering during development is
Hyprland-specific (`grim`, `hyprctl -j clients`). The macOS equivalent is
`screencapture -l$(...)` or just looking at the window.

## The honest caveat about the platform

This is a *Linux* platform. Developing it on macOS means testing against a GTK
backend that is not the target: Wayland fractional scaling, client-side
decorations, portals and compositor behaviour are all different or absent.

That matters more than it might sound. `react-native-gtkx`'s author develops
from macOS via a Linux VM, and native Linux testing was a genuine differentiator
for this project. If the Mac becomes the primary machine, a Linux VM (or the
Arch box over SSH) is worth keeping in the loop for anything touching layout,
input, scaling or accessibility.

## Working notes

`plan/` is in the repo: milestone plans (`02-reacthost-surface.md` has the
detailed next step, including the `ReactHost` constructor arguments read out of
Fantom's `TesterAppDelegate`), a backlog of known gaps, and a decisions log
recording why Path A, why clang, and why `RnLayout` does no layout.

## Not in this repo

Everything `bootstrap.sh` regenerates: `third_party/` (folly, fast_float,
nlohmann_json, hermes, hermes-build, codegen) and `build/`.

Two bits of machine state also do not travel, both handled by `bootstrap.sh`:
React Native's `node_modules` (its `yarn install` also rewrites `yarn.lock` in
the RN checkout), and a Node version satisfying RN's engines.
