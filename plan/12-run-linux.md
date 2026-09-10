# Phase 12 — `react-native run-linux`

> **Started, 2026-09-10.** The command runs an app. It does not build the host.

`react-native run-linux` now starts a packager if one is not running, launches
the host, and stays attached so Ctrl-C reaches the app. Verified against
`examples/demo`, an app that resolves this platform the way somebody else's
project would.

## What it does

Everything `run-android` does except the build:

- Finds the host binary. Explicit `--host-binary`, then `RN_LINUX_HOST`, then
  `<project>/linux/build`, `<project>/build`, and finally the build tree of a
  development checkout. When none of those has one, it prints the three commands
  that produce one rather than a path that does not exist.
- Reuses a packager already on the port, or starts one, or stays out of the way
  with `--no-packager`.
- Reads the `AppRegistry` name from `app.json`, which `--module` overrides,
  because Expo apps register `main` whatever the app is called.
- Writes a release bundle for `--mode release`, and a fallback bundle in
  development, since the host loads that file when Metro cannot be reached and a
  missing one turns a packager problem into a mystery.

## What it does not do

**It does not build the host**, which is the difference between this and a real
platform command. `run-android` hands the build to Gradle and `run-ios` to
Xcode; here the host is a CMake project in this repository, built against one
specific React Native, and the npm package does not contain it. Making the
command build it means moving the C++ sources, the CMake files and the bootstrap
into the package, which is what react-native-windows does with `vnext/`. That is
the next real step and it is a large one.

**There is no autolinking.** `dependencyConfig` returns null. Nothing on this
platform has native code to link yet, and claiming otherwise would generate
references to files that do not exist.

## The one field that looks right and is wrong

`platforms.linux` deliberately has no `npmPackageName`.

Setting it is what marks a platform *out-of-tree* to React Native's CLI, and it
is what react-native-windows and react-native-macos both do. The CLI responds by
installing `reactNativePlatformResolver`, which rewrites every
`react-native/...` import to `react-native-linux/...` while bundling for linux,
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
of this package at `/src/react-native-linux/packages/react-native-linux`, so it
concluded the package was already watched and Metro then refused to read the
files the plugin hands it. That is exactly the layout this repository is
developed in, which is why it survived being written and tested the same day.

**A detached packager holding the terminal.** Metro was started detached with
inherited stdout, so anything reading `run-linux`'s output waited forever for a
process meant to outlive the command. This repository's own CI workflow carries
a warning about the identical shape. Metro's output now goes to
`.rn-linux/metro.log`, whose path is printed.

## Next

1. Build the host from the package, so `run-linux` works from an npm install.
2. Publish, which needs the above and a decision about prebuilt binaries versus
   building from source on first run.
3. Autolinking, which is blocked on porting one native module end to end.
