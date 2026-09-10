# Phase 11 — running against a released React Native

> **Done, 2026-09-10.** Verified against 0.87.1 and against `main`.

Until now this platform built against React Native `main`, whose package version
is `1000.0.0`, React Native's placeholder for "not a release". So it had never
run against a React Native anyone ships. `plan/10-first-real-app.md` found that
by pointing the host at a real application; this is the fix.

**Result:** 60 unit tests and all 5 end-to-end scenarios pass against React
Native 0.87.1 and against `main`, from the same source tree.

## What actually stood in the way

Four things, none of which was the one predicted.

**The build assumed `main`'s target list.** 0.87.1 has no ResizeObserver, so two
CMake targets did not exist and configuration failed outright. A release is
`main` minus whatever landed after it, so this will happen again with every
version and every new upstream directory. Missing directories and targets are
now skipped and *printed*, because building less than intended in silence is the
worse failure.

**Codegen output is version-specific and was cached as though it were not.**
The generated spec headers name every feature flag that version has, so 0.87.1
built against artifacts generated from `main` failed on flags `main` had added
since, deep inside React Native's own sources, complaining about members that
look like they should exist. `third_party/codegen` now carries a stamp of the
version it came from; bootstrap regenerates on a mismatch, and CMake refuses to
configure with a clear message and the exact command rather than letting the
compiler produce a mystery.

**React could not render at all**, because `HermesInstance` gives the runtime a
microtask queue only when `enableBridgelessArchitecture()` is true, and React's
scheduler enqueues a microtask on its first render. That flag defaults to `true`
on `main` and `false` on 0.87.1, so the first commit threw

> Could not enqueue microtask because they are disabled in this runtime

The host now states the flag instead of inheriting a default that moves. It is
bridgeless -- there is no bridge here to be the alternative -- so it should have
been asserting that all along. iOS and Android override feature flags at
startup too; relying on the default was the anomaly.

**And the platform lied about its version.** ReactCxxPlatform's
`PlatformConstantsModule` hardcodes a React Native version of 1000.0.0, in every
version, releases included. React Native's development builds compare that
against the version the bundle came from, so every developer on a release would
see "React Native version mismatch. JavaScript version: 0.87.1, Native version:
1000.0.0" forever, along with advice that cannot help. `src/LinuxPlatformConstants.cpp`
replaces that module and reports the version CMake read out of React Native's
own `package.json`.

That last one has a trap worth remembering. The generated C++ spec binds each
method to the class it is templated on, at compile time, so subclassing
ReactCxxPlatform's module and overriding `getConstants` compiles, runs, and does
nothing: the method table still points at the base. It has to derive from the
spec itself. The symptom is indistinguishable from the module never being
consulted.

## What this cost, and what it bought

The host now supplies its own TurboModules through
`ReactCxxTurboModuleProvider`, which consults them before its own. That seam was
sitting empty. It is also the seam an Expo port would use, so proving it works
is worth more than the one module it currently carries.

Testing two versions needed the build directory to stop being hardcoded.
`scripts/bundle.sh` and `scripts/integration_test.py` both take `--build-dir`
now, so a second React Native gets a second build tree and the suites run
against either.

## Supported versions

| Version | State |
|---|---|
| `main` | works, and is what development happens against |
| 0.87.1 | works, fully verified |
| 0.83 to 0.86 | untested, expected to work: they share the unguarded module lookup |
| 0.82 and older | needs `RN$TurboInterop`; see `plan/backlog.md` |

Nothing has been done for 0.82 and older, which is where kino sits. That is a
deliberate stopping point rather than an oversight: the flag is verified to fix
it, but supporting a version means testing it, and each one is a checkout, a
bootstrap and a build.

## What is not covered

- **CI checks one version per run.** The primary job now pins 0.87.1, because a
  red tick then means "broken for someone who could actually use this" rather
  than "broken against a moving target". `main` moves to the weekly drift job,
  which does not block. That is a real trade: a regression that only affects
  `main` now waits up to a week. Reverse it by pointing `scripts/react-native.pin`
  back at a commit.
- Nothing tests 0.83 through 0.86, so the table above is a claim about two
  versions and a prediction about four.
- The version this host reports cannot express a prerelease tag, because the
  Android spec it answers types `prerelease` as an optional int. Stable releases
  have none, so this only matters for a release candidate.
