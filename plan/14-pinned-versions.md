# Pinning a triple instead of following the app

> **Done, 2026-09-10.** `packages/react-native-linux/supported-versions.json`.

Trying to run kino, which pins React Native 0.81.6, turned into chasing version
differences one compile error at a time. This is the decision that stopped it.

## What following the app looked like

The host had just learned to build from an installed React Native by fetching
`ReactCxxPlatform` at the app's exact version, since the npm package omits it.
The obvious next move was to derive everything else from the app the same way.
Applied to 0.81.6, in order:

1. Hermes failed to compile: our pinned version wanted a `jsi/hermes-interfaces.h`
   that 0.81's `ReactCommon` does not have.
2. So Hermes was read from the app instead. React Native records it in
   `sdks/.hermesversion` on 0.81 and `sdks/.hermesv1version` on 0.87, and both
   ship in the package, so this worked.
3. Then that Hermes failed differently: its CMake has no `hermesvm` target,
   which is the one bootstrap builds.

Three layers in, for a version nobody had claimed to support. Each fix was
correct and the direction was wrong.

## Why the counterparts do not have this problem

Checked against clones of both rather than assumed.

Neither uses submodules. React Native for Windows compiles against the app's
installed React Native, pointing at `node_modules/react-native/ReactCommon`,
which is what this platform now does too. React Native for macOS is a full fork,
so React Native's C++ is simply in its tree.

The difference is Hermes. Windows pins a Hermes **NuGet package version** in its
own property sheet, and pins `react-native` in `peerDependencies` to one exact
nightly build. macOS, iOS and Android all consume a prebuilt Hermes as well.
Meta publishes one for each of those platforms and none for Linux, so this is
the only platform that compiles Hermes from source. Every version problem above
comes from that one difference.

The pin lives on their side, not the app's. That is the thing worth copying.

## What we do now

`supported-versions.json` names, for each supported React Native, the Hermes to
build. Bootstrap reads it before downloading or compiling anything, and refuses
an unsupported version in seconds with a message that says what is supported and
where to look.

Today that is React Native 0.87.x, verified against 0.87.1, and `main` for
development.

`ReactCxxPlatform` still comes from the app's exact version rather than the
table, and that is not an inconsistency: within a supported minor it is the
thing that keeps the C++ and the JavaScript in step, which is the property
`plan/11-released-versions.md` exists to protect. The table bounds *which*
minors, and the fetch keeps us exact *within* one.

## What this costs, honestly

Adding a React Native version is now a deliberate act: build against it, run
both suites, add an entry. It cannot be done by widening a range, and that is
the point. 0.83 through 0.86 were previously described as "expected to work",
which was a prediction wearing the clothes of support; they are now listed as
untested and refused.

kino stays unrunnable. It pins 0.81.6, and supporting that was attempted and
stopped; see below.

## 0.86, which is what an Expo app installs

Added deliberately, because a current Expo app installs React Native 0.86.3 and
the point of this platform is that Expo apps can reach the desktop. Kino sits at
0.81 because react-native-macos does, not because Expo does.

It builds and runs: 60/60 unit tests and four of five end-to-end scenarios.

The fifth is **Fast Refresh, and it cannot work on 0.86**. `ReactHost`'s
teardown sets `reactInstanceData_->mountingManager` to null, along with the
context container and the TurboModule providers. A reload destroys the instance
and recreates it, so the new one comes up with no mounting manager and the host
collapses three milliseconds after the refresh arrives. React Native 0.87
deleted exactly those three lines, which is what makes the same test pass there.
Reproduced by hand, root-caused against the two sources, and recorded against
the version rather than treated as ours.

Two guards were also needed in this project's own sources: `TextAlignment` gains
`Start` and `End` in 0.87, and `TextMeasureCacheKey` gains `pointScaleFactor` in
0.87. Both thresholds were first guessed at 0.83 and 0.86 disproved the guess,
which is the argument for checking against tags rather than assuming.

## What supporting 0.81 turned out to cost

Attempted deliberately after the table was in place, and abandoned with the
evidence rather than the guess. Six differences, in the order they appeared:

1. Its Hermes builds `libhermes`, not `hermesvm`, and React Native links
   `hermes-engine::libhermes`. **Fixed**, and generically: the build now accepts
   either name.
2. Its codegen has no `-f` flag, so core artifacts go to a fixed folder inside
   React Native itself, and it exits non-zero after printing "Done" because it
   then fails to stat an output directory it never used. **Fixed**, by judging
   the run on what it produced.
3. `TextAlignment` has no `Start` or `End`, and `TextMeasureCacheKey` has no
   `pointScaleFactor`. **Fixed**, with a version macro and two guards.
4. `BaseTouch` has no `timeStamp`. Another guard, not written.
5. There is no `react/renderer/animationbackend`, only `react/renderer/animations`,
   so `GtkAnimationChoreographer` would have to be compiled out entirely.
6. `ReactHost` takes eleven parameters rather than twelve, takes
   `TurboModuleManagerDelegates` rather than `TurboModuleProviders`, and has no
   choreographer parameter at all. That is a second host construction path, not
   a guard.

And underneath all of it: **0.81 has no websocket client**. It ships
`IWebSocketClient` with nothing behind it; the boost implementation arrived
later. So no packager connection, no Fast Refresh, and no websockets for the
app. Writing one is larger than everything above put together.

The first three were worth doing regardless and are kept: they make the build
tolerant rather than 0.81-specific, and the fifth of them uncovered a real
latent bug, a `SYSTEM` include hardcoded to a path that does not exist when
React Native is installed rather than checked out.

The rest is a permanent second code path through the host, for the version that
would also be the most degraded. That is a decision about one app, not a gap to
close on principle, and kino would still stop at Expo afterwards.
