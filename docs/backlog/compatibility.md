# Compatibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. React Native 0.82 and older cannot reach a native module
2. React Native 0.83 through 0.86 are refused rather than untested-but-allowed
3. Supporting React Native 0.81, which is what kino pins
4. CI checks one React Native per run
5. Expo starts but no Expo module works
6. No @shopify/react-native-skia, which stops one real app dead
7. The version this host reports cannot express a prerelease tag

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
  upstream in ReactCxxPlatform.

  **"0.87.1 works without any of this" is wrong, and the flag is needed on
  `main` too.** Measured 2026-09-26 against React Native `main`, bundling
  `js/skia.js`: without the flag, `getEnforcing('RNSkiaModule')` fails exactly as
  it does on 0.81; with it, the module resolves and Skia installs. So this is not
  an old-version curiosity -- **every third-party TurboModule reached through
  `getEnforcing` needs it**, on every version this supports. React Native's own
  modules are unaffected because they are asked for differently, which is why
  nothing noticed. That makes `__turboModuleProxy` the load-bearing upstream item
  rather than a tidiness one, and it is the single largest gap between this
  platform and "install the package and it works".
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
- **No `@shopify/react-native-skia`**, which is the thing that stops kino dead --
  and it is closer than "not this platform's to fix" suggested. Established by
  bundling kino's `apps/desktop` against basalt and running it, 2026-09-26:

  The bundle builds with no errors at all: Metro resolves kino's whole tree,
  Expo included, through `withDesktopPlatforms`. The app then fails at
  `TurboModuleRegistry.getEnforcing('Networking')`, which is the 0.81 gate above
  and nothing to do with Skia. With `globalThis.RN$TurboInterop = true`
  prepended, Networking resolves, `FormData` stops being missing, the app reaches
  `Running "main"`, and the next and only blocker is
  `getEnforcing('RNSkiaModule')`. So the version gate is one line and Skia is the
  whole remaining distance.

  What Skia actually needs here, from reading what it ships:

  - **Skia itself is not the problem.** `libs/macos` holds prebuilt binaries and
    `cpp/{api,jsi,rnskia}` is host-agnostic C++ over JSI.
  - **`RNSkiaModule` is 61 lines.** One `RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install)`
    that builds a `SkiaManager` from an `RCTBridge` and a `CallInvoker`, and
    `SkiaManager` wraps `RNSkia::RNSkManager`. basalt has a runtime and a call
    invoker; what it lacks is an `RNSkPlatformContext`, and
    `RNSkApplePlatformContext` exists and is Metal and CoreGraphics -- so this is
    adapting the places it reaches for `RCTBridge`, not writing it.
  - **`<Canvas>` is the real work**, and it is separable. `SkiaPictureView` and
    `SkiaUIView` are written against `RCTViewComponentView`, so an AppKit host
    needs its own view hosting a `CAMetalLayer` plus the component registered in
    `AppKitMountingManager`.

  Which stages: the module alone unblocks Skia's imperative API -- `Skia.Path`,
  image decoding, typefaces -- which is what kino's `runtime/images.ts`,
  `runtime/media.ts` and `store.ts` use. `<Canvas>` is only needed for
  `ui/Preview.tsx` and `ui/AnchorsPane.tsx`.

  GTK and Win32 are a different proposition: no prebuilt Skia, and a GL, Vulkan
  or D3D window context rather than Metal.
- **The version this host reports cannot express a prerelease tag.** The Android
  spec it answers types `prerelease` as an optional int, so there is nowhere to
  put `-rc.1`. Stable releases have none, so this only matters for somebody
  testing against a release candidate.
