# 38 — Reanimated, and the mount nobody reported

The last of the fifteen. Reanimated is the biggest of the three libraries and,
in the end, the least *written*: unlike gesture-handler it ships a large body of
portable C++, and the work was compiling somebody else's sources and finding the
four things they expect a platform to hand them.

It comes in two packages. `react-native-worklets` is the runtime -- a second
Hermes on the UI thread, the serialisation that gets a closure into it, the
schedulers between the two. `react-native-reanimated` is the animation library
on top of it.

## Worklets, which very nearly just worked

Thirty-one translation units, compiled unmodified, with two defines. The whole
host half is four things:

- a **UI scheduler**, which is `postToUiThread` and the base class's queue;
- a **frame source**, for its animation frames;
- the **platform logger** its C++ declares and leaves to each platform;
- the **TurboModule** its JavaScript looks up, hand-written for the reason
  `core/GestureHandlerModule.h` gives -- a third-party spec is generated during
  *that library's* build against *an app's* node_modules, and these hosts are
  generic binaries not built per app.

Then a worklet ran:

    worklets ran on the UI runtime, 2 + 2 = 4

Which is the whole of it: a function serialised out of the JavaScript runtime,
rebuilt in a second Hermes on the UI thread, run there, and its result handed
back. The hard part was written once, portably, by the people who wrote it. A
platform contributes a thread and a heartbeat.

## Reanimated compiled too, with one type

A hundred and seven translation units, and one thing missing. Reanimated's
`Common/cpp` is portable to exactly two platforms: about ninety `#ifdef ANDROID`
/ `#ifdef __APPLE__` branches run through it, and one of them decides a *type*
rather than an implementation --

    #ifdef ANDROID
    using SynchronouslyUpdateUIPropsFunction = ...
    #elif __APPLE__
    using SynchronouslyUpdateUIPropsFunction = ...
    #endif

-- with a struct member of that type declared unconditionally. On a third
platform it does not compile. `core/ReanimatedCompat.h` is force-included ahead
of everything and declares the alias first, after which the `#elif` chain skips
a definition that already exists. Nothing is patched and nothing is vendored.

**That file is written and not yet exercised.** The Linux host in this
repository is built on macOS, where `__APPLE__` is defined and the compat header
compiles to nothing. The first build that reaches it will be a real Linux one.

Reanimated also needs codegen artifacts it does not ship -- its C++ includes
`react/renderer/components/rnreanimated/Props.h`, which React Native generates
from its TypeScript specs during a library's native build. `cmake/Reanimated.cmake`
runs the same generator, against the package's own `codegenConfig`, at configure
time. It is the first time this project has run codegen for anything other than
React Native itself, and it is what a real autolinking step would do.

## The bug this found, which was not Reanimated's

With all of it compiled and wired, an animation ran perfectly and moved nothing.
The shared value updated, `withTiming` reported `finished=true`, the
`useAnimatedStyle` worklet ran on every frame with the right numbers -- and the
view did not move, on either desktop.

`performOperations` was running sixty times a second and flushing an empty
batch. Reanimated's mount hook was registered and never called. Which led here:

    Scheduler::reportMount(SurfaceId)  →  UIManager::reportMount

iOS calls it from `RCTSurfacePresenter` after each mounted transaction, Android
from its mounting manager, and **ReactCxxPlatform calls it nowhere**. Nothing in
the shared platform had a mount hook until a third-party library brought one, so
nothing noticed. Both mounting managers report the mount now, and the animation
moved.

Worth reporting upstream: any library that registers a `UIManagerMountHook` is
silently inert on the cxx platform.

## And a bug that was ours

The dump that proved the animation had not moved also could not have proved that
it *had*: a view's transform was not in it. Adding it -- the six numbers of the
2D affine part, in the order CSS writes `matrix()` -- showed something else.
**The macOS host had never applied transforms at all.** `AppKitMountingManager`
had a `TODO(props): ... transform ...` where GTK had thirty lines, and every
comparison the project makes between the two hosts had been blind to it, because
a rotated view and an unrotated one printed the same line.

So: `RnAppKitView` sets `layer.transform` from React Native's matrix, which is
CATransform3D's sixteen numbers in the same order. The demo's rotated card now
prints

    transform=(0.939693,0.34202,-0.34202,0.939693,0,0)

on both desktops, which is cos 20° and sin 20°, and the comparison can see it.

## What it can do

    rea ended at 200,150
    view tag=6 ... bg=#56c98aff transform=(1,0,0,1,200,150)

A pan gesture whose callbacks are worklets, driving a shared value, driving an
animated style -- the box moved exactly the injected drag, with no React
re-render. And a scroll handler:

    view tag=6 ... transform=(1,0,0,1,100,0)
    view tag=256 ... scroll=(0,400)

`useAnimatedScrollHandler` dividing the offset by four. That one needed the last
piece of host wiring: `Scheduler::addEventListener`, which is how Reanimated
sees an event before the components do. Without it a scroll handler is declared,
attached, and never called.

Identical on both hosts.

## What is honest about it

- **The frame is a timer**, sixteen milliseconds, not the display's refresh.
  Each platform's display link belongs to React Native's own
  `AnimationChoreographer`, which pauses whenever React Native has no animation
  of its own -- which is exactly when a Reanimated one might be running. A frame
  source the two can share is the fix and is in the backlog.
- **`synchronouslyUpdateUIProps` is a no-op.** Reanimated's other path, direct to
  a mounted view, skips the shadow tree; this platform's mounting managers only
  accept mutations from a Fabric transaction, which is what makes their thread
  rule assertable. The shadow-tree path is the one used here and it is the one
  that works everywhere.
- **Sensors, keyboard events, screen snapshots and pseudo-selectors are no-ops**,
  each as a named field rather than a silent omission: there is no
  accelerometer, no soft keyboard, and the other two are iOS's answers to iOS
  problems.
- **Layout animations and shared element transitions are untested.** They
  compile; nothing here has run one.

Fifteen of fifteen.
