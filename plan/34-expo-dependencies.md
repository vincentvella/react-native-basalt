# 34 — what a real app's dependencies do

Phase 29 ran a stock `create-expo-app` on both desktops and it worked. That
template imports `expo-status-bar` and nothing else, and the note said so: "no
`expo-image`, no `expo-router`, no Reanimated, no gesture-handler, no
third-party native module of any kind. Those are the next questions."

This is those questions. Same app, plus the libraries a real one has:

    expo-image  expo-font  expo-constants  expo-linking  expo-asset
    expo-haptics  expo-clipboard  react-native-safe-area-context
    react-native-gesture-handler  react-native-reanimated
    react-native-screens  @react-navigation/native  @react-navigation/native-stack

`App.js` requires each one inside its own try/catch, at call time, and renders
the result -- the same shape as `js/probe.js`, for the same reason: a top-level
import that throws takes the app down, which is the failure being looked for.

## The build failed before anything ran

    Unable to resolve module ./TabsScreen from
    react-native-screens/src/components/tabs/screen/index.ts

`react-native-screens` ships `TabsScreen.ios.tsx`, `TabsScreen.android.tsx` and
`TabsScreen.web.tsx`, and an `index.ts` that says `from './TabsScreen'`. There
is no neutral file, so on a platform the library has never heard of the
resolution fails -- and one component that nothing in the app renders takes down
a build that would otherwise have worked.

This is not a react-native-screens bug. Every library that has never heard of
this platform is a candidate, which is all of them, so it cannot be a list of
names. `metro-config.js` now retries a failed resolution as another platform:
Android first, then iOS, the first that resolves wins, and the original error is
what gets thrown if both fail. One order for all three desktops, because two
desktops resolving *different* implementations of the same library is the one
outcome worse than either. Each fallback is reported once:

    basalt: './TabsScreen' has no macos implementation; using its android one

Android first for the same reason `SELF_IMPORTING_SHIMS` are answered with their
`.android.js` sibling. The implementation will not have its native module either
way; what the fallback buys is that the app builds and everything else in it
works, and the unsupported library fails at runtime, which is a much better
place to fail than someone else's `index.ts`.

## Nine of fifteen, identically on both

| Library | macOS | Linux | |
|---|---|---|---|
| `expo` | ok | ok | |
| `expo-status-bar` | ok | ok | |
| `expo-asset` | ok | ok | |
| `expo-font` | ok | ok | imports; loading a font is untested here |
| `expo-haptics` | ok | ok | imports; there is nothing to vibrate |
| `react-native-safe-area-context` | ok | ok | |
| `react-native-screens` | ok | ok | via the Android fallback |
| `@react-navigation/native` | ok | ok | |
| `@react-navigation/native-stack` | ok | ok | |
| `expo-constants` | **null** | **null** | `Constants.expoConfig` is null |
| `expo-image` | **fails** | **fails** | `Cannot find native module 'ExpoImage'` |
| `expo-linking` | **fails** | **fails** | `Cannot find native module 'ExpoLinking'` |
| `expo-clipboard` | **fails** | **fails** | `Cannot find native module 'ExpoClipboard'` |
| `react-native-gesture-handler` | **fails** | **fails** | `getEnforcing('RNGestureHandlerModule')` |
| `react-native-reanimated` | **fails** | **fails** | `Cannot read property 'loadUnpackers' of undefined` |

The two hosts agree line for line, which is the same result phase 29 got and is
still the thing worth checking.

## What "fails" costs

It is fatal to the app, and the probe's try/catch is what hid that. Importing
`expo-image` at module scope the way every real app does:

    [js error] [runtime not ready]: Error: Cannot find native module 'ExpoImage'
    [js error] [runtime not ready]: Invariant Violation: "main" has not been registered.

The bundle threw while evaluating, so `AppRegistry.registerComponent` never ran,
so the surface has nothing to mount. The process stays up and the window stays
empty. There is no red box, and the only evidence is in the host's log.

## What this actually says

Three of the five failures are the *same* failure: an Expo SDK library with
native code calls `requireNativeModule('ExpoSomething')`, and this platform
registers no Expo modules at all. Phase 15 installed `globalThis.expo` -- the
runtime, the event emitter, the shared-object plumbing -- and no modules on top
of it. So the missing piece is not expo-image, or clipboard, or linking. It is
an **Expo module host**: a way for C++ to register something
`requireNativeModule` can find, after which each library is its own small piece
of work rather than a fork of Expo.

Two of them, clipboard and linking, are already implemented here for React
Native's own `Clipboard` and `Linking` modules (phase 32). The work is the
registration, not the behaviour.

Gesture-handler and Reanimated are a different and larger shape: both are native
libraries in their own right, both want a worklet runtime, and neither has
anything to do with Expo's module system.

`expo-constants` is the odd one out and the cheapest: `Constants.expoConfig` is
null because nothing supplies the app manifest. An app that reads
`Constants.expoConfig.extra.apiUrl` -- a common thing -- crashes on the property
access rather than on the import.

None of this is fixed here. It is measured, on both desktops, against real
versions, and it is the list the next phases come from.
