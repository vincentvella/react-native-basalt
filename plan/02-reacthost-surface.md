# Phase 2 — ReactHost + Hermes + a live surface

> **Done, 2026-09-08.** `src/main.cpp` and `js/demo.js`. What the plan got
> wrong, and the answers to the questions left open below, are recorded at the
> bottom of this file and in `plan/decisions.md`.

**Goal:** a GTK window running a real React Native surface, with the JS side
being a hand-written script rather than a Metro bundle. Success is the mutation
stream arriving from Fabric instead of from `mount_harness_gtk`.

## Why this is the hard step

Everything so far avoided a JS runtime. This phase adds Hermes, `ReactInstance`,
the scheduler, and the run loop — i.e. most of what makes RN *run*.

## Work

### 1. Build Hermes

Not in the current dependency closure. Options, cheapest first:

- Build `hermesvm` standalone from `facebook/hermes` with CMake. It is
  self-contained (no LLVM dependency) and builds on Linux — it is the Android
  engine.
- Version must match RN's pin. Check `packages/react-native/sdks/.hermesversion`
  or the `hermes-engine` gradle module before building anything.

Then add to the CMake closure: `react/runtime`, `react/runtime/hermes`,
`hermes/executor`, `hermes/inspector-modern`, `jsiexecutor`.
Fantom's `tester/CMakeLists.txt` is the authoritative list — diff against it.

### 2. Extend the third-party layer

Fantom additionally needs `gflags`, `nlohmann_json`, and OpenSSL. `gflags` is
installed; add `nlohmann_json` to `cmake/ThirdParty.cmake` the same way as
fast_float.

### 3. RunLoopObserverManager → GTK frame clock

**Corrected 2026-09-08:** this does *not* drive mounting. `executeMount` comes
from the JS thread via `RuntimeScheduler::updateRendering`; the mounting
manager now marshals to the main thread itself. `RunLoopObserverManager`
creates the `EventBeat` that flushes events in step with the run loop, and
`AnimationChoreographer` (separate, `resume()`/`pause()`/`onAnimationFrame`) is
where the frame clock belongs for animations.

Still needs wiring to `gdk_frame_clock_*`, but for events and animation, not
for mounts.

- `gtk_widget_get_frame_clock(root)` once the root is realised.
- Connect to `GdkFrameClock::update` (or `::paint`), and drive the observer
  from there.
- `gdk_frame_clock_begin_updating()` while a surface is running.

Watch for: the frame clock only exists after realisation, so `startSurface`
cannot happen in `activate` before the window is presented.

### 4. Construct ReactHost

```
ReactHost(
    ReactInstanceConfig,          // bundle path / dev server settings
    mountingManager,              // ours, already written
    runLoopObserverManager,       // (3)
    contextContainer,
    onJsError,                    // log + eventually RedBox
    logger,                       // route to g_log
    devUIDelegate = nullptr,      // later
    turboModuleProviders = {},    // later
    logBoxSurfaceDelegate = nullptr,
    ...)
```

Then:

1. `createSurfaceRoot(surfaceId)` and parent it into the window.
2. `loadScript(bundlePath, sourcePath)`.
3. `startSurface(surfaceId, moduleName, initialProps, layoutConstraints)`.
4. Drive `setSurfaceConstraints` from `GtkWidget::size-allocate` on the window.
5. `stopSurface` on window close; `stopAllSurfaces` on shutdown.

### 5. Minimal JS

Hand-written, no Metro yet — enough to call `AppRegistry.registerComponent` and
render a tree of `<View>`s. Precompile to Hermes bytecode with `hermesc` to
avoid needing a parser path first.

## Answered from TesterAppDelegate.cpp

- **`ReactInstanceConfig`** is small: `appId`, `deviceName`, `enableDevMode`,
  `enableInspector`, `devServerHost`, `devServerPort`, and an *optional*
  `platformTimerRegistryFactory` (unset falls back to the thread-based
  `PlatformTimerRegistryImpl`). Fantom only overrides the timer factory to get
  determinism; a real host can leave it alone.
- **`ContextContainer` must be populated**, and this is not optional. Fantom
  inserts `MessageQueueThreadFactoryKey`, `HttpClientFactoryKey`,
  `WebSocketClientFactoryKey`, `DevToolsHttpClientFactoryKey`,
  `DevToolsWebSocketClientFactoryKey`. It stubs the app-facing http/websocket
  factories but uses the *real* ReactCxxPlatform ones for DevTools; we want the
  real ones for both, and `MessageQueueThreadImpl` rather than `StubQueue`.
- **`TurboModuleProviders`** takes a list. Fantom passes a custom lambda plus a
  platform provider. We need the ReactCxxPlatform default plus, eventually,
  `ImageLoaderModule`.
- **`AnimationChoreographer`** defaults to `nullptr` but Fantom always supplies
  one. It is an abstract class with `resume()`/`pause()` pure virtual, plus
  `now()` and `onAnimationFrame(timestamp)`. This is the natural home for the
  GTK frame clock.

## Answered by doing it

- **`turboModuleProviders = {}` is fine.** `ReactHost` wraps whatever it is
  given in `ReactCxxTurboModuleProvider`, which supplies the core modules
  itself. An empty list means "no extra modules", not "no modules".
- **No barrier is needed.** Fantom's `flushMessageQueue()` exists because its
  queue is a `StubQueue` that only runs work when pumped. With the default
  `MessageQueueThreadImpl` the queue is a real thread that drains on its own.
  Ordering still holds: `startSurface` registers the shadow tree synchronously
  on the calling thread, so scheduling the JS commit after it is enough.
- **`size-allocate` does not exist in GTK4.** Step 4 above assumed it. The
  layout manager's `allocate` is the replacement; see `plan/decisions.md`.
- **http and websocket are not optional.** `ReactHost` throws without both
  factories in the ContextContainer, and ReactCxxPlatform defines neither.
- **`stopSurface` always calls into JS**, even for a surface started with an
  empty module name, so the script must install `RN$stopSurface`.

## Still open

- `Scheduler::reportMount` is never called; see `plan/backlog.md`.

## Done when

A GTK window shows a `<View>` tree whose mutations came from Fabric, driven by
JS executing in Hermes, with `mount_harness_gtk` no longer involved.
