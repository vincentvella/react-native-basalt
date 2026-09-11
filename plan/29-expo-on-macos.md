# Phase 29 — a stock Expo app on macOS

> **Study, 2026-09-11.** No code changed. A `create-expo-app` renders on macOS,
> and its tree is identical to the same app on Linux.

Phase 15 showed a stock Expo app running on Linux. This asks the only question
that could still surprise us: does the second desktop get it for free, or was
the first one carrying something nobody noticed?

## The subject

`npx create-expo-app --template blank`, unmodified: Expo 57.0.22, React Native
0.86.3, both stock from npm. 464 packages. 0.86 is one of the two versions
`supported-versions.json` pins, so the host was built against
`react-native-0.86` rather than against `main`.

## What it took

Three lines in `metro.config.js`:

```js
const {getDefaultConfig} = require('expo/metro-config');
const {withDesktopPlatforms} = require('react-native-basalt/metro-config');

module.exports = withDesktopPlatforms(getDefaultConfig(__dirname));
```

Two symlinks standing in for an `npm install` of packages that are not published
yet, and a host built with `-DBASALT_EXPO_MODULES_CORE` pointed at the app's own
`expo-modules-core`. Nothing else. `App.js` is untouched.

Worth noticing: Expo's default Metro config **already lists `macos`** among its
platforms, because react-native-macos exists. So `withDesktopPlatforms` added
`linux` and `windows` and found `macos` already there -- and the overrides still
applied, because the plugin keys off the platform being one of ours rather than
off having added it.

## What happened

Everything, first try.

`basalt-bundle --platform macos` produced a bundle on the first attempt. The
host loaded it, `AppRegistry` ran `main`, and the text rendered centred in a
white window.

Then the part that matters: the same app bundled for `linux` and run through the
GTK host produces a **tree identical to the macOS one**, frames aside. Two
desktops, one stock Expo app, no per-platform code anywhere.

That is the answer to the question this phase asked. The second platform got it
for free. Everything Expo needed had already been made portable -- the runtime
in phase 15, the asset pipeline in 16, the shared core in 17 -- and none of it
needed touching for a second view layer to use it.

## What it asked for and did not get

Eight native modules, and the list is short enough to read as a to-do:

| Module | What it is | Matters? |
|---|---|---|
| `Appearance` | dark mode | **Yes.** Unimplemented on both desktops, and the first thing a real app notices. |
| `BlobModule` | `Blob`, `File`, `FileReader` | **Yes**, for anything that fetches binary data. |
| `DeviceEventManager` | hardware back button | No. There is no back button on a desktop. |
| `SoundManager` | UI click sounds | No. |
| `NativeUnimoduleProxy` | Expo's legacy bridge | No. Superseded by `globalThis.expo`, which does exist. |
| `ExpoUpdates`, `ExpoGo`, `EXDevLauncher` | OTA updates, Expo Go, the dev launcher | No. All three are looked up optionally and all three are about running *inside Expo's own clients*, which a desktop host is not. |

Every one of them is a warning rather than a failure: Expo's JavaScript looks
them up optionally and carries on. That is not luck -- it is what
`TurboModuleRegistry.get` is for, as against `getEnforcing`, and phase 10 found
the hard way what happens when a platform gets that distinction wrong.

## What this does not show

**One template.** `--template blank` uses `expo-status-bar` and nothing else.
No `expo-image`, no `expo-router`, no `expo-font` (which phase 16 proved
separately on Linux and has never been run on macOS), no Reanimated, no
gesture-handler, no third-party native module of any kind. The nine hundred
modules of phase 10's subject are still untested on either desktop.

**No interaction.** It rendered. Nothing pressed, scrolled or typed into it.

**A release bundle, not a dev one.** Metro was never started; Fast Refresh on
macOS remains wired and unexercised.

**No assets.** The template has an `assets/` directory and the blank app
references none of it, so "0 asset files from 0 assets" proves the bundler ran
and nothing about the asset pipeline on macOS.

Those are the next questions, and they are the same next questions Linux has.
