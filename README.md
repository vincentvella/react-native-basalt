# react-native-linux (skeleton)

A React Native out-of-tree platform for Linux: Fabric's mounting layer rendered
onto GTK4 widgets.

This is a skeleton, not a working platform. It exists to make the shape of the
work concrete.

## Why this is tractable

React Native already ships a generic C++ platform that is neither iOS nor
Android: `packages/react-native/ReactCxxPlatform/`, a sibling of `ReactAndroid`
and `ReactApple`. It provides `ReactHost` (instance lifecycle, surfaces,
reload), `SchedulerDelegateImpl`, and implementations of http, io, logging,
threading, profiling, devsupport, coremodules and TurboModule hosting.

`private/react-native-fantom/tester/` is a CMake-built C++ host that already
runs on Linux — its `CMakeLists.txt` has an explicit `if(UNIX AND NOT APPLE)`
branch, and `getHostPlatform.js` maps `process.platform === 'linux'` to a
supported host. So Hermes, Yoga and Fabric already build and run here.

`ReactHost`'s constructor takes a `std::shared_ptr<IMountingManager>`. That
interface has exactly two pure-virtual methods — `executeMount` and
`dispatchCommand`. Implementing it against GTK4 is the core of this project.

## Layout

    src/RnView.h/.cpp           GTK4 widget layer. No RN dependency.
    src/GtkMountingManager.*    IMountingManager implementation.
    src/demo_layout.cpp         Renders hand-written frames; no RN needed.

`RnView` is a `GtkWidget` subclass paired with `RnLayout`, a `GtkLayoutManager`
that performs no layout: Yoga has already resolved absolute frames by the time
a mutation arrives, so `RnLayout::allocate` places each child at the rect the
shadow tree assigned it, and `measure` reports zero so GTK never second-guesses
Yoga.

## Mutation semantics

Read off `StubViewTree::mutate`, RN's own reference walk:

| Mutation | `parentTag` | Meaning |
|---|---|---|
| `Create` | `-1` | Allocate a widget, register by tag. Not attached yet. |
| `Delete` | `-1` | Unregister and drop the last reference. |
| `Insert` | parent | Attach an existing child at `index`. |
| `Remove` | parent | Detach, but do **not** destroy — a `Delete` may follow, or a re-`Insert`. |
| `Update` | parent | New props / layout metrics for an existing tag. |

Two consequences the implementation depends on:

- Create/Delete are registry operations; Insert/Remove are tree operations. A
  view can sit in the registry with no parent between a Remove and its Delete,
  so the registry holds a strong reference (`g_object_ref_sink` on Create).
- `mutation.mutatedViewIsVirtual()` marks views that exist only in the shadow
  tree to keep an `EventEmitter` alive. They have no widget; skip them on
  Insert and Remove.

Fabric emits no `Create` for a surface root — the root shadow node is the base
of every diff. The host calls `createSurfaceRoot(surfaceId)` before
`ReactHost::startSurface`. In Fabric a `SurfaceId` *is* the root node's tag,
which lets the root live in the same registry as every other view.

## Threading

`executeMount` arrives on the **JS thread**, from the event loop's "update the
rendering" step (`RuntimeScheduler_Modern::updateRendering`, called with the
`jsi::Runtime` live). GTK widgets are main-thread-only, so `executeMount` only
queues the transaction onto a GLib idle source; `applyTransaction` does the
widget work on the main thread and asserts it got there. iOS and Android
marshal at the same point.

Queuing is unconditional, even when the caller is already the main thread:
equal-priority GLib idle sources run in insertion order, which is what
preserves mutation ordering.

## Build

    cmake -B build -G Ninja -DRN_DIR=/path/to/react-native/packages/react-native
    cmake --build build

Produces `librn_view.a`, `librn_mounting.a` and the `demo_layout` executable.
Omit `-DRN_DIR` to build only the widget layer and the demo.

### Dependencies

System packages (Arch): `gtk4 pango google-glog boost gflags fmt
double-conversion cmake ninja`.

folly is fetched rather than installed, pinned to the version RN builds
against (`packages/react-native/gradle/libs.versions.toml`):

    mkdir -p third_party && cd third_party
    curl -L https://github.com/facebook/folly/archive/v2024.11.18.00.tar.gz | tar xz
    mv folly-2024.11.18.00 folly

Only folly's headers are used here, so it does not need to be built.

### Version drift

RN pins glog `0.3.5`, boost `1_83_0`, fmt `12.1.0`. Arch ships glog `0.7.1`,
boost `1.92.0`. One real incompatibility surfaced: glog >= 0.6 added a guard
requiring `GLOG_USE_GLOG_EXPORT` to be defined when the header is not consumed
through its CMake target, which RN's pinned 0.3.5 predates. Without it every
glog header fails with *"was not included correctly"*. The define is set in
`CMakeLists.txt`. Boost 1.92 caused no problems.

RN headers also carry Xcode's `#pragma mark`, so GCC needs
`-Wno-unknown-pragmas`.

## Status

Verified, on screen:

- `mount_harness` builds RN's C++ core (220 objects) and drives **real
  `ShadowViewMutation`s** through the real `GtkMountingManager` into real GTK4
  widgets. A before/after pair confirms each mutation type:
  Create+Insert place three views and a nested child; Update recolours a view
  *and* changes its frame; Remove+Delete removes one entirely; untouched views
  stay put. The nested child survives its parent's Update and overflows the
  parent's shortened frame, which is correct for `overflow: visible`.
- No JS runtime is involved. Hermes is not in the dependency closure at all.
- `GtkMountingManager.cpp` compiles under `-Wall -Wextra` with zero
  diagnostics; `static_assert(!std::is_abstract_v<GtkMountingManager>)` holds.

Not yet done:

- No `ReactHost`, no Hermes, no Metro. Mutations are hand-built, not produced
  by React reconciling a component tree.
- Only `<View>` has a GTK peer. `hasComponent` and the component registry say
  so honestly.

## Building React Native's core

The closure of `react_renderer_mounting` is 29 RN targets, computed from RN's
own `target_link_libraries`. `cmake/ReactNativeCore.cmake` adds them;
`cmake/ThirdParty.cmake` supplies the third-party target *names* RN links
against, since a host build has no gradle step to download them.

Six things a host has to get right, none of them documented:

1. **Use clang, not GCC.** RN builds with `-Wall -Werror -Wpedantic` and is a
   clang codebase. Under GCC it fails on `#pragma mark` (Xcode-ism), on folly's
   `__int128` under `-Wpedantic`, and on `-Wsubobject-linkage` in
   `NetworkIOAgent`. Under clang all of it compiles.
2. **`-latomic`.** `std::atomic<std::optional<double>>` in
   `ReactNativeFeatureFlagsAccessor` is 16 bytes wide; those are out-of-line
   calls. Fantom's `CMakeLists.txt` carries the same `if(UNIX AND NOT APPLE)`
   branch.
3. **Platform-variant include dirs must be global.** Each seam with a
   `platform/{android,ios,cxx}` split needs its variant on the include path
   before any RN target is added, or targets like `componentregistry` cannot
   find `HostPlatformViewProps.h`.
4. **OBJECT libraries do not propagate objects through an INTERFACE target.**
   `target_link_libraries` carries usage requirements only; the objects need
   `$<TARGET_OBJECTS:...>` naming explicitly, or the link comes up short on
   every symbol they define.
5. **`getDefaultComponentRegistryFactory()` is not implemented anywhere in
   ReactCommon.** It is declared, and no `.cpp` in the tree defines it: each
   host writes its own, and in doing so declares which components its platform
   supports. See `src/LinuxComponentRegistry.h`. Fantom has its own version.
6. **`JSIDynamic.cpp` is compiled by no CMakeLists in the tree**, yet
   `RawProps` references `jsi::dynamicFromValue`. Hosts must build it.

### Upstream bug found

`ReactCommon/jsinspector-modern/network/HttpUtils.h` uses `uint16_t` without
including `<cstdint>`. It builds on Meta's toolchains only via transitive
includes. Worked around with a force-include rather than patching the checkout.

### Version drift

RN pins glog `0.3.5`, boost `1_83_0`; Arch ships glog `0.7.1`, boost `1.92.0`.
glog >= 0.6 added a guard requiring `GLOG_USE_GLOG_EXPORT` when its headers are
not consumed through its own CMake target, which RN's pin predates -- without
it every glog header hard-errors. Boost 1.92 caused no problems.

### Note on rendering

Under Hyprland with Omarchy's window rules the window is composited
translucently, so screenshots show the wallpaper through the views. That is a
compositor opacity rule, not the renderer.

## Next

1. `main.cpp`: construct `ReactHost` with this mounting manager, wire
   `RunLoopObserverManager` to the GTK frame clock, `createSurfaceRoot`, then
   `startSurface` with `setSurfaceConstraints` driven by the window size. This
   adds Hermes (`react/runtime` + `react/runtime/hermes`) to the build.
2. Load a Metro bundle and get `<View>` with flexbox on screen with Fast
   Refresh -- the first milestone where React itself is doing the reconciling.
3. Replace the stub `TextLayoutManager` (`textlayoutmanager/platform/cxx`
   measures nothing and returns `layoutConstraints.minimumSize`) with Pango.
   Largest single remaining piece.
4. `IImageLoader`, input/gestures, then the accessibility hooks
   `IMountingManager` already declares against AT-SPI.

## Caveat

`ReactCxxPlatform` carries no public API stability guarantee -- Fantom builds it
with `RN_BUILDING` to reach private includes. It will churn. The mitigation is
that Meta keeps it working for their own CI.
