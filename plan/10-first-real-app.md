# Phase 10 — pointing the host at a real app

> **Study, 2026-09-10.** No code changed except one packaging fix; see the end.

The roadmap had phase 10 as "`run-linux` CLI, packaging", and the ordering
against LogBox and the missing components was a guess. This replaces the guess
with a list, by taking a real React Native application and running it.

The subject is **kino**, a video editor: Expo 54, `react-native-macos` 0.81.7,
React Native 0.81.6, Skia for all rendering, two Swift Expo modules, and a
local daemon. Roughly nine hundred modules. It is not a toy, and it was not
written with this platform in mind, which is the point.

## What happened, in order

**It bundled.** `withLinuxPlatform` wrapped kino's own Metro config — Expo's
defaults, rnx-kit, its resolver conditions — and produced a 1.6MB `linux`
bundle on the second attempt. Nothing in the nine hundred modules failed to
resolve. That is a better result than expected, and it is the platform package
earning its keep.

**Every native module lookup failed.** The first thing the bundle does is read
`Platform`, and it died on `TurboModuleRegistry.getEnforcing('PlatformConstants')`.
This is the important finding and it is not about `PlatformConstants`; see the
next section.

**Then Expo's native runtime was missing.** With that worked around, the bundle
got as far as `globalThis.expo.EventEmitter`, which is undefined because
`expo-modules-core` installs it from native code that does not exist here.

Skia, the Swift modules and the daemon were never reached.

## The version wall, which is the real finding

The host installs `global.nativeModuleProxy`, and `nativeModuleProxy.PlatformConstants`
resolves correctly. It does **not** install `global.__turboModuleProxy`. Both
were confirmed by probing the live runtime rather than by reading code.

React Native's `TurboModuleRegistry` tries the proxy first and falls back to
`NativeModules`. On `main` that fallback is unconditional, so the host works. On
0.82 and earlier it is gated:

```js
if (
  global.RN$Bridgeless !== true ||
  global.RN$TurboInterop === true ||
  global.RN$UnifiedNativeModuleProxy === true
) {
  const legacyModule = NativeModules[name];
```

This host sets `RN$Bridgeless` and neither of the others, so on those versions
the guard is false, the fallback never runs, and **no native module of any kind
can be reached.** Not `PlatformConstants`, not networking, not `DevSettings`.
The first one touched is simply the one that reports it.

Setting `globalThis.RN$TurboInterop = true` before the bundle evaluates fixes it
outright, verified. Installing `__turboModuleProxy` would be the more honest fix
and belongs upstream in ReactCxxPlatform.

**Correction, same day.** The first version of this document said no released
React Native could reach a native module, and that the platform ran against
`main` alone. That was an overstatement, written before checking when the guard
was removed. Checked since, against the tags:

| Release | Fallback |
|---|---|
| 0.83.0 and newer, through 0.87.1 | unguarded, works with this host as-is |
| 0.82.0 and older, including kino's 0.81.6 | guarded, needs the flag |

So the guard was removed in 0.83, and the newest releases may need nothing at
all. That makes the flag a question about how far back to support rather than
the blocker for every release, and it moves the real question to whether this
host compiles and runs against a release at all. The lesson is the one this
project keeps relearning: check the range before describing it.

## What the app needed that we already have

kino uses eight things from `react-native`, and seven of them work:

| Used | State |
|---|---|
| `View`, `Text`, `StyleSheet` | fine |
| `Pressable` | fine |
| `ScrollView` | fine |
| `TextInput` | fine, except the static focus registry below |
| `Image` | fine; it loads remote http thumbnails, which this platform does |
| `PanResponder` | **verified working** |

`PanResponder` deserves its own line. It is how kino does every drag — the
timeline, the preview, the inspector, the pane dividers — and it was the thing
most likely to be quietly broken, since taps were all this platform had ever
been driven with. A real X11 press, six pointer moves and a release produced
exactly the expected gesture state and moved the view by exactly the distance
dragged.

And the app reaches its daemon over loopback http and a websocket. Both of those
this platform has.

## What the app needed that we do not have

In the order they would bite:

1. **Released React Native versions**, per above, and kino's own 0.81.6 is on
   the side of the line that needs the flag.
2. **Expo's native runtime.** `expo-modules-core` expects `globalThis.expo`
   installed from native code. kino uses Expo for very little — `registerRootComponent`
   and `requireNativeModule` — but the import is unconditional, and so is that
   of most of the ecosystem. This is the difference between "runs React Native"
   and "runs React Native applications as people write them".
3. **Skia.** Every pixel kino draws goes through `@shopify/react-native-skia`,
   imported at module scope in six files. Not a gap this platform can close on
   its own.
4. **Key events.** kino's entire keyboard story is
   `focusable` / `keyDownEvents` / `onKeyDown`, which are `react-native-macos`
   props. So there is nothing to be compatible *with* here, but a desktop app
   that cannot read a keypress is not a desktop app.
5. **`TextInput.State.currentlyFocusedInput()`**, which kino calls and guards.
   Already in the backlog.

## What this changes

The guess was that missing components and LogBox were what stood between this
and a real application. For this application that is wrong, and interestingly
so. kino uses no `Modal`, no `Switch`, no `ActivityIndicator`, no `FlatList`, no
`Animated` — it hand-rolls its overlays out of absolutely positioned views. The
component surface was not the wall. The ecosystem was.

So phase 10 should lead with compatibility rather than packaging:

1. Make released React Native versions work, and pick a supported range.
2. Then decide about Expo, which is a much larger question than it looks and
   probably wants its own study.
3. `run-linux` and packaging after that, because publishing something that only
   runs against React Native `main` would be publishing a curiosity.

LogBox did not come up, because the failures here were runtime errors before
first render, where LogBox would not have helped. It stays worth doing and it is
not urgent.

## The one thing fixed along the way

`withLinuxPlatform` handed Metro files out of its own package without adding
that package to `watchFolders`, so Metro refused to hash them and bundling
failed. Invisible in this repo, where `packages/` is watched already, and
immediate for anyone consuming the platform from a linked checkout or another
monorepo.
