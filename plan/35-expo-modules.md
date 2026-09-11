# 35 — an Expo module

Phase 34 measured a real Expo app's dependencies and found that six of fifteen
libraries fail, three of them the same way: `requireNativeModule('ExpoImage')`,
`'ExpoLinking'`, `'ExpoClipboard'` -- an Expo SDK library asking for its native
half, on a platform that registers no Expo modules at all. At module scope,
which takes the app down before `AppRegistry` runs.

Phase 15 installed `globalThis.expo` -- the classes, the event emitter, the
shared-object plumbing -- and an empty registry. This fills the registry.

## The seam, and the one that looks easier

`requireOptionalNativeModule` looks in three places:

```js
globalThis.expo?.modules?.[moduleName] ??
  NativeModulesProxy[moduleName] ??
  createTurboModuleToExpoProxy(TurboModuleRegistry.get(moduleName), moduleName)
```

The third is tempting: this project already has TurboModules, and one named
`ExpoClipboard` would be found. But the wrapper builds its module by copying
properties off `Object.getPrototypeOf(turboModule)`, and a C++ TurboModule is a
jsi host object whose prototype is `Object.prototype`. It would copy nothing.
That path is for React Native's *codegenned JavaScript* TurboModules, which are
real JS objects with a real prototype.

So the first is the seam, and it is the one every native platform uses.

A module is an instance of expo's own `NativeModule` class --
`expo::NativeModule::createInstance`, which ExpoRuntime already installs --
with host functions set on it. The class is where `addListener` comes from, and
expo-linking calls `addListener` whether or not anything ever emits. The whole
of `core/ExpoModules.cpp`'s framework is that plus a three-line `addFunction`;
the rest of the file is behaviour, and there is very little of it because the
behaviour was already written.

## What a missing method should do

`ExpoClipboard` implements text: `getStringAsync`, `setStringAsync`,
`hasStringAsync`. It does not implement images or URLs, which are separate
pasteboard types on each platform.

The interesting part is how it does not implement them. Expo's JavaScript checks
for each function before calling it, and reports:

    The method or property Clipboard.getImageAsync is not available on macos,
    are you sure you've linked all the native dependencies properly?

That is a better answer than a stub returning null, and it names the method. So
a method this platform cannot implement is **left off** rather than faked. The
absence is the API.

The clipboard itself is `core/PlatformServices.h`'s, the same one React Native's
own `Clipboard` module uses -- `NSPasteboard` on macOS, `GdkClipboard` on Linux.
Setting a string from an Expo app and reading it back with `pbpaste` outside the
app shows the real system clipboard, which is the only way to know.

## The manifest, and where a desktop's "build step" is

`ExpoLinking` needs two functions, because `openURL` and `canOpenURL` go through
React Native's own `Linking`, which this platform has. Adding them produced:

    expo-linking needs access to the expo-constants manifest (app.json or
    app.config.js) to determine what URI scheme to use.

`Constants.expoConfig` is the app's resolved Expo config, and it was null
because nothing supplied it. iOS and Android embed it during the **native
build**, from a script expo-constants contributes to the Xcode and Gradle
projects. This platform's hosts are generic binaries with no per-app build step,
so there is no equivalent moment there.

The equivalent moment is **bundling**, which is the only step that has both the
project and somewhere to put the answer. So `bundleWithAssets` writes the
resolved config as `app.config.json` beside the bundle, and the host reads it
from beside the bundle as it starts. `isPublicConfig`, which is Expo's own flag,
drops `hooks` and the EAS credentials under `extra`: embedding the private
config would be a change in what an app ships, made silently by a bundler.

The dev path writes it on every run rather than only when it writes the fallback
bundle. An `app.json` edited since would otherwise be invisible in development
and applied in release, which is the worse half of a difference between the two.

expo-constants treats any manifest object without a `metadata` key as embedded
and hands it back as `expoConfig`, so this is all it takes. The native constants
beside it are deliberately few: expo-constants spreads that object into
`Constants`, so a field invented here is a field an app will read and believe.
`executionEnvironment` is `'bare'`, which is accurate -- bare is "a native app
that happens to use Expo modules", as against Expo Go or a dev client.

## Where it got to

Nine of fifteen libraries became twelve, identically on macOS and Linux:

    clipboard: round trip through expo-clipboard
    hasString: true
    image unavailable, as expected: ...not available on macos...
    expoConfig.name: expoapp
    executionEnvironment: bare
    createURL: expoapp://some/path
    linking listener: added and removed

Three are left, and none of them is this shape:

- **expo-image** is a *view*, not a module. It needs a Fabric component, which
  is a different port with a different seam -- the component registry, prop
  parsing, a mounting peer.
- **Reanimated** and **gesture-handler** are native libraries in their own
  right, both wanting a worklet runtime, neither having anything to do with
  Expo's module system.

And one thing that is this shape and is not done: neither desktop delivers a URL
to a running app, so `getLinkingURL` is honestly null. macOS needs an Apple
Event handler and a registered scheme; Linux a desktop entry and single-instance
activation. Now that the module exists, that is the only work left for deep
links.
