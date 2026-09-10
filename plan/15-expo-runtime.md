# Phase 15 — Expo's runtime

> **Done, 2026-09-10.** A stock Expo app renders on Linux.

The point of this platform is that a desktop target can track React Native
without forking it. The thing that would make that matter is Expo apps reaching
the desktop, since that is how React Native apps are written now. So Expo is the
work, and kino was a poor first subject: it drags a canvas library and a local
daemon, and its React Native version is pinned by react-native-macos rather than
by Expo.

The subject is `npx create-expo-app --template blank`: Expo 57.0.21, React
Native 0.86.3, React 19.2.3. Nothing modified except a Metro config.

## Where it gets to

On screen, with no JavaScript errors:

```
view tag=1 frame=(0,0 900x700)
  view tag=6 frame=(0,0 900x700) bg=#ffffffff
  view tag=4 frame=(308.5,342.5 283x15) clip text="Open up App.js to start working on your app!"
```

Centred to the half-pixel in both directions, which means Yoga laid out Expo's
template exactly as it would anywhere else.

Getting there took two things. Expo's runtime, below. And `StatusBarManager`,
which `expo-status-bar` reaches for and which is looked up with `getEnforcing`,
so its absence threw and took the first render with it. A desktop has no status
bar, so that module now exists and does nothing, and reports a height of zero --
literally, because React Native's own JavaScript lays out around that number and
a plausible-looking phone height would push every app's content down by a bar
that is not there.

## The part worth reporting

**expo-modules-core ships its portable C++ in the npm package.** `common/cpp`
holds `EventEmitter`, `SharedObject`, `SharedRef`, `NativeModule` and their JSI
helpers, and depends on nothing but JSI and a call invoker. So there is nothing
to vendor and nothing to fetch: this compiles the app's own copy, at the app's
own Expo version.

That is better than the arrangement `expo-desktop` uses for macOS and Windows,
which vendors that directory at a pinned SDK commit and inherits the pin. It is
also the same principle that makes the React Native side work: build against
what the app has, and its C++ and its JavaScript cannot drift apart.

Notably it is *not* what React Native itself does, where the equivalent
directory is excluded from the package and has to be fetched separately. Expo
ships the portable half; React Native does not.

## What is installed, and what is a stub

Four classes, following Expo's own Android installer exactly:

```
EventEmitter::installClass
SharedObject::installBaseClass   (with a releaser that frees nothing)
SharedRef::installBaseClass
NativeModule::installClass
```

Then `expo.modules`, holding `ExpoAsset` and `ExponentConstants` as **stubs**,
plus an empty `NativeModulesProxy`. The macOS and Windows ports stub the same
pair for the same reason: Expo's own start-up reaches for them before an app's
code runs. Anything that calls into them will fail, and should.

The releaser frees nothing because nothing here owns a native Expo object. No
Expo module is ported. `expo-font`, `expo-image`, and every config plugin's
native half are each their own piece of work, and this does not pretend
otherwise.

## Two packaging problems the app found

Both would hit any real consumer, and neither had shown up in this repository.

- **The package declared no dependencies.** Its compiled JavaScript uses Babel
  runtime helpers, and nothing said so.
- **Metro could not resolve the package's dependencies when it is linked rather
  than installed.** It resolves this package's files by their real path and then
  looks for their imports by walking up from there, which lands in this
  repository rather than in the app. `withLinuxPlatform` now names the project's
  `node_modules` explicitly, which covers the linked and monorepo cases as well
  as the installed one.

## What this does and does not demonstrate

It demonstrates the thesis: an application written the way React Native
applications are written now, unmodified, on a desktop platform that forks
nothing. The React Native underneath is stock 0.86.3 from npm, and the Expo
underneath is stock 57.0.21.

It does not demonstrate that Expo *modules* work, because none do. The blank
template uses no native Expo module, which is exactly why it was the right first
subject and exactly why it is not the last one.

## `expo-font`, the first real Expo module

Ported, and it works. `ExpoFontLoader` is no longer a stub: `loadAsync` puts a
real font file into fontconfig, which is where Pango looks, so `fontFamily`
works for text this platform renders. The same string at the same size measures
323x33 in a loaded Courier New against 270x29 in the default sans, which is the
font actually being used rather than merely registered.

Its non-web surface is three methods -- `loadAsync`, `getLoadedFonts` and
`isLoaded`. `isLoaded` is documented as web-only and is not: `Font.js` throws
unless it is a function.

Two things had to be true beyond loading the file.

**The name the app chooses is not the name in the file.** An app says
`ProbeFont`; the file says `Courier New`; fontconfig indexes by the latter. So
`src/LinuxFonts.cpp` reads the family back out of the file and keeps the
mapping, and the Pango layer resolves through it on every measurement. Ordinary
system families pass straight through.

**Pango caches which fonts exist, and so do we.** Adding a font to fontconfig
behind Pango's back leaves it looking at the old list, so the font map is told
the configuration changed. And text measured before a font arrived was measured
against a different set of fonts, so those measurements have to be dropped --
which React Native's own measurement cache cannot do, having no way to be
emptied, so this file keeps its own.

Even with both, a font loaded after first render only takes effect on text
mounted afterwards. That is not a gap: React does not re-render a `<Text>` whose
props did not change, so Fabric never re-measures it, and the same is true on
iOS and Android. It is why `useFonts` returns a flag and asks you to render
nothing until it is true.

## What still does not work: `useFonts` itself

`Font.loadAsync` does not reach our module, because it goes through `expo-asset`
first: `Asset.fromModule(require('./font.ttf'))`, then `downloadAsync()`, then
our `loadAsync` with the resulting local path. That throws inside expo-asset
before any of it.

The asset registry itself is fine -- `require()` yields an id and
`getAssetByID` returns the asset. What is missing is the resolution and
retrieval underneath, which is the `require()`d-assets gap already in
`plan/backlog.md`, and it is not Expo-specific: any module handed a bundled file
will hit it.

So the module is ported and the pipeline that feeds it is not. Calling
`ExpoFontLoader.loadAsync` with a path works today; `useFonts` will work when
assets do.

## Next

1. The asset pipeline. It blocks `useFonts`, `expo-image`, and every `<Image>`
   with a `require()`d source, and it is the same piece of work for all of them.
2. The rest of the missing core modules in `plan/backlog.md`. Appearance,
   clipboard, linking and alerts are the ones an app notices next.
