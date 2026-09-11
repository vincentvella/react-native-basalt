# Phase 16 — the asset pipeline

> **Done, 2026-09-10.** `useFonts` with a `require()`d font works from a clean
> bundle, with nothing placed by hand.

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

## Emitting them

`basalt-bundle` builds a bundle and writes its assets beside it. One
command, and the same one for every kind of app.

React Native's CLI could have done half of it -- it has `--assets-dest` -- but
only an app that *has* that CLI can reach it, and an Expo app does not: `expo`
is its CLI and does not know this platform, while Metro's own `build` command
has no asset option at all. Since the code had to exist for Expo apps anyway, it
became the only path, so both kinds of app get identical output and there is one
place for assets to go wrong. `run-linux` calls the same function.

It does what React Native's CLI does internally: build, then ask a Metro
`Server` for `getAssets`, then copy each file to the location its
`httpServerLocation` and scale imply. Two details matter.

`metro/private/*` is how Metro's package exports its internals, and `Server` is
not otherwise reachable -- `metro/src/Server` is blocked by its exports map.

And the destination rule has to agree exactly with what React Native computes at
runtime in `scaledAssetPath`, down to the `@2x` suffix and the `../` to `_`
replacement for assets outside the project root. Disagreeing means files land
where resolution does not look, and the symptom is a missing file with no
explanation.

Metro is looked up from the project first and from React Native second. An
installed app has it hoisted; a checkout or a workspace may not, and it is
always beside React Native.

## What is left

**Network assets are not fetched.** A dev server serves assets over http and
`downloadAsync` rejects saying so. The host has an http client already, so this
is plumbing rather than design, but it is untouched.

**Nothing is cached.** `downloadAsync` returns the file where it lies, which is
right for a local asset and will not be right for a remote one.
