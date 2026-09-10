# Phase 15 — Expo's runtime

> **Started, 2026-09-10.** A stock Expo app starts, and renders until it asks
> for a React Native module this platform does not have.

The point of this platform is that a desktop target can track React Native
without forking it. The thing that would make that matter is Expo apps reaching
the desktop, since that is how React Native apps are written now. So Expo is the
work, and kino was a poor first subject: it drags a canvas library and a local
daemon, and its React Native version is pinned by react-native-macos rather than
by Expo.

The subject is `npx create-expo-app --template blank`: Expo 57.0.21, React
Native 0.86.3, React 19.2.3. Nothing modified except a Metro config.

## Where it gets to

- **It bundles**, with one line on top of Expo's own Metro config.
- **Expo's runtime installs**, so `globalThis.expo` is there and the first
  import no longer kills start-up.
- **React renders.** The failure is now a component stack several `View`s deep.
- **It stops at `StatusBarManager`**, a React Native core module this platform
  does not provide, which the blank template uses through `expo-status-bar`.

That last one is not an Expo problem. It is one of the missing core modules
already listed in `plan/backlog.md`, and a desktop has no status bar, so the
honest implementation is a module that exists and does nothing. It is not a
two-minute fix only because the codegen spec for it is not among the artifacts
this build generates.

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

## Next

1. `StatusBarManager`, and the rest of the core modules in `plan/backlog.md`.
   That is what stands between here and a blank Expo app on screen.
2. Then an Expo app that uses an actual Expo module, which is the first real
   test of the module system rather than of start-up.
