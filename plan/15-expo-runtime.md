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

## Next

1. An Expo app that uses a real Expo module -- `expo-font` or `expo-image` --
   which is the first test of the module system rather than of start-up.
2. The rest of the missing core modules in `plan/backlog.md`. `StatusBarManager`
   was the one in the way; appearance, clipboard, linking and alerts are the
   ones an app notices next.
