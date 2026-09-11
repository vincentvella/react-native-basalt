# Phase 12 — `react-native run-linux`

> **Started, 2026-09-10.** The command runs an app, and builds the host when
> asked, from a React Native checkout.

`react-native run-linux` now starts a packager if one is not running, launches
the host, and stays attached so Ctrl-C reaches the app. Verified against
`examples/demo`, an app that resolves this platform the way somebody else's
project would.

## What it does

Everything `run-android` does except the build:

- Finds the host binary. Explicit `--host-binary`, then `BASALT_HOST`, then
  `<project>/.basalt/build` where `--build` puts one, then `<project>/linux/build`
  and `<project>/build`, and finally the build tree of a development checkout.
  When none of those has one, it says how to make one rather than printing a
  path that does not exist.
- Builds it, with `--build`.
- Reuses a packager already on the port, or starts one, or stays out of the way
  with `--no-packager`.
- Reads the `AppRegistry` name from `app.json`, which `--module` overrides,
  because Expo apps register `main` whatever the app is called.
- Writes a release bundle for `--mode release`, and a fallback bundle in
  development, since the host loads that file when Metro cannot be reached and a
  missing one turns a packager problem into a mystery.

## What it does not do

**There is no autolinking.** `dependencyConfig` returns null. Nothing on this
platform has native code to link yet, and claiming otherwise would generate
references to files that do not exist.

## The one field that looks right and is wrong

`platforms.linux` deliberately has no `npmPackageName`.

Setting it is what marks a platform *out-of-tree* to React Native's CLI, and it
is what react-native-windows and react-native-macos both do. The CLI responds by
installing `reactNativePlatformResolver`, which rewrites every
`react-native/...` import to `react-native-basalt/...` while bundling for linux,
and by requiring the package to export `./setup-env`.

Both are right for those platforms, because both are forks that vendor React
Native's JavaScript: react-native-macos replaces the `react-native` package
outright, and react-native-windows tracks 123 copied files in an `overrides.json`
manifest against a pinned base version. This package vendors none of it. It
redirects nine of React Native's own shims to their existing `.android.js`
siblings and overrides two files. So the rewrite pointed every import at a
package that does not contain it, and the failure named a React Native file
rather than the redirect that caused it.

Leaving the field out keeps `linux` a platform the CLI accepts while resolution
stays in `withLinuxPlatform`, which is where this platform's policy actually
lives.

## Two bugs it turned up

**A path prefix test without a separator.** `withLinuxPlatform` adds its own
directory to `watchFolders` unless it is already covered, and asked that with
`startsWith`. A React Native checkout at `/src/react-native` is a string prefix
of this package at `/src/react-native-basalt/packages/react-native-basalt`, so it
concluded the package was already watched and Metro then refused to read the
files the plugin hands it. That is exactly the layout this repository is
developed in, which is why it survived being written and tested the same day.

**A detached packager holding the terminal.** Metro was started detached with
inherited stdout, so anything reading `run-linux`'s output waited forever for a
process meant to outlive the command. This repository's own CI workflow carries
a warning about the identical shape. Metro's output now goes to
`.basalt/metro.log`, whose path is printed.

## The native code moved into the package

Everything that was at the root of this repository -- `CMakeLists.txt`,
`cmake/`, `src/`, `tests/` and `bootstrap.sh` -- now lives in
`packages/react-native-basalt/native/`, because npm can only ship what is inside
the package directory. That is the same shape react-native-windows uses, where
the package *is* `vnext/`.

`CMakeLists.txt` at the root is now a wrapper. It contributes the two things a
checkout knows and an app must not assume: that `third_party` belongs at the
root of the repository, and that the binary should appear at
`build/basalt_gtk`, which every script and document here names. So the way
this repository has always been built is unchanged.

Three things had to stop being assumed:

- **Where `third_party` lives.** `BASALT_THIRD_PARTY` now says, and bootstrap
  takes the same path. An app gets `.basalt/third_party` under itself, never a
  directory inside `node_modules`, which npm rewrites on install and which is no
  place to leave a Hermes build.
- **That the codegen `CMakeLists.txt` is checked in.** It is ours rather than
  generated -- codegen writes one that links Android-only targets -- so it moved
  into the package and bootstrap installs it.
- **That `reactNativePath` is a real directory.** Resolving the monorepo above
  it walked up from a symlink and landed in the app. Workspaces, pnpm and
  `npm link` all produce that, so it resolves the link first now.

`run-linux --build` then vendors the dependencies, configures, builds, and runs.
Verified from `examples/demo`: 282MB of vendored dependencies and a 37MB host,
built and launched from the app rather than from this repository.

It is opt-in rather than automatic, because a first build compiles Hermes and
React Native's C++ core, and that is not something a command should begin on its
own because a binary happened to be missing.

## Still missing

**It needs a React Native source checkout**, and says so clearly when it does
not have one. Every app has an installed React Native and almost none has a
checkout, so this is the gap between "an app can build the host" and "an app
someone else wrote can build the host".

The reason is narrower than it first looks, and worth stating exactly, because
the obvious guess is wrong. An installed `react-native` *does* ship C++: its
`ReactCommon`, the codegen script, the Hermes pin and the version table are all
there, which is how Android builds React Native out of `node_modules`. What it
does not ship is **`ReactCxxPlatform`**, which is excluded from the package's
`files` list -- and that is the layer this entire host is built on: `ReactHost`,
the scheduler delegate, the TurboModule provider, http, dev support.

So closing it means one of three things: persuading React Native to ship
`ReactCxxPlatform` in the package, which is the smallest change and the one that
helps every out-of-tree C++ platform; vendoring that directory into this package
the way expo-desktop vendors expo-modules-core; or shipping prebuilt hosts per
React Native version.

## Next

1. Make it work from an installed React Native, per above.
2. Publish, which needs the above and a decision about prebuilt binaries versus
   building from source on first run.
3. Autolinking, which is blocked on porting one native module end to end.
