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

kino stays unrunnable. It pins 0.81.6, which needs `RN$TurboInterop` for any
native module to resolve *and* a Hermes whose build this bootstrap cannot drive,
and it then stops at Expo's native runtime regardless. Supporting it is a
decision to support 0.81, not a bug to fix.
