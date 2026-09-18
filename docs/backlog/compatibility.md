# Compatibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (6):**

1. React Native 0.82 and older cannot reach a native module
2. React Native 0
3. Supporting React Native 0
4. CI checks one React Native per run
5. Expo starts but no Expo module works
6. No @shopify/react-native-skia, which is not this platform's to fix, but is wor

Found by bundling and running a real application.

- **React Native 0.82 and older cannot reach a native module.** The host
  installs `global.nativeModuleProxy` but not `global.__turboModuleProxy`, and
  relies on `TurboModuleRegistry` falling back to `NativeModules`. On `main` and
  on releases from 0.83 that fallback is unconditional; on 0.82 and earlier,
  which includes kino's 0.81.6, it is gated behind
  `RN$Bridgeless !== true || RN$TurboInterop === true ||
  RN$UnifiedNativeModuleProxy === true`, all of which are false here. So every
  `getEnforcing` fails on those versions, starting with `PlatformConstants`.
  Setting
  `globalThis.RN$TurboInterop = true` before the bundle evaluates fixes it and
  is verified; installing `__turboModuleProxy` is the better answer and belongs
  upstream in ReactCxxPlatform. 0.87.1 works without any of this, so the
  supported range starts there; see `docs/PORTING.md`. Extending
  it downwards means testing each version, not just setting the flag.
- React Native 0.83 through 0.86 are refused rather than untested-but-allowed.
  Each would need building against and both suites run; see
  `docs/PORTING.md` and `supported-versions.json`.
- Supporting React Native 0.81, which is what kino pins, needs three separate
  things: `RN$TurboInterop` so any native module resolves, a Hermes whose CMake
  names its target `hermes` rather than `hermesvm`, and then Expo's native
  runtime before kino itself would start. It is a decision, not a bug.
- **CI checks one React Native per run.** It pins 0.87.1, and `main` moved to
  the weekly drift job, which does not block. So a regression that only affects
  `main` can wait up to a week. Building both on every run would be the fix and
  would double CI cost on a private repo.
- **Expo starts but no Expo module works.** `globalThis.expo` is installed now, from the
  app's own expo-modules-core, so an Expo app starts and renders. What does not
  exist is any Expo *module*: `ExpoAsset` and `ExponentConstants` are stubs that
  exist only so Expo's start-up survives, and `expo-font`, `expo-image` and
  every config plugin's native half are each their own port.
- No `@shopify/react-native-skia`, which is not this platform's to fix, but is
  worth knowing as the thing that stops one real app dead.
