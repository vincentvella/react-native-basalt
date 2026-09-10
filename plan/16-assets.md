# Phase 16 — the asset pipeline

> **Runtime half done, 2026-09-10.** `useFonts` works with a `require()`d font.
> The bundler half is not: assets are not yet emitted for an Expo app.

`require('./logo.png')` had never worked on this platform. It blocks
`<Image source={require(...)}>`, `expo-font`'s `useFonts`, `expo-image`, and
anything else handed a bundled file, and it is the same piece of work for all of
them.

## What it turned out to be

Not one gap but a chain, and the first link was the surprising one.

**React Native learns where its assets are from where its *script* came from.**
`AssetSourceResolver` asks whether the bundle came from a server or a file, and
answers `require('./logo.png')` with either a URL on that server or a path
beside that file. Both come from `SourceCode.getConstants().scriptURL`.

On React Native 0.86 that string is always empty. `ReactHost` has no
`sourceURL_` at all; 0.87 added one. So every asset resolved to nonsense, and
the nonsense was quiet: `file://assets/assets/probe-font.ttf`, a URL with an
empty host and a relative path, which fails at the point of use rather than at
the point of resolution.

`src/LinuxSourceCode.cpp` reports the URL from the host, which has known it all
along. That works on every supported version rather than only the ones that
happen to fill it in, and it is the same seam the platform constants module
uses.

**Then `ExpoAsset.downloadAsync` has to exist.** For a `file://` URL there is
nothing to download -- React Native already resolved it beside the bundle -- so
the work is confirming the file is there and handing the URL back. The check
matters: without it a missing asset succeeds and fails much later, somewhere
that has no idea which file was meant. It now says which path it wanted.

## Where it gets to

`useFonts({ ProbeFont: require('./assets/probe-font.ttf') })`, the real Expo
API with a real `require()`, loads the font and text renders in it: 323x33
against 270x29 for the same string in the default sans.

The one manual step was putting the file where the bundle expects it, because
nothing emits it there yet.

## What is left

**Assets are not emitted at bundle time.** React Native's own CLI has
`--assets-dest`, and `run-linux` now passes it, pointing beside the bundle,
which is where resolution looks. That covers a React Native app.

It does not cover an Expo app, which has no React Native CLI -- `expo` is its
CLI, and Metro's own `build` command has no asset option at all. That needs the
programmatic path React Native's CLI uses internally: a Metro `Server`, its
`getAssets`, and copying each file to the location its `httpServerLocation` and
scale imply. That belongs in this package, since an Expo app has no other way to
reach it.

**Network assets are not fetched.** A dev server serves assets over http and
`downloadAsync` rejects saying so. The host has an http client already, so this
is plumbing rather than design, but it is untouched.

**Nothing is cached.** `downloadAsync` returns the file where it lies, which is
right for a local asset and will not be right for a remote one.
