# 36 — expo-image, and how an Expo view becomes a component

Phase 35 filled `globalThis.expo.modules`, which is where an Expo module's
*functions* live. expo-image is not that shape: `Image` is a view, and a view
does not go in the module registry.

## What `requireNativeViewManager` actually does

    const NativeExpoImage = requireNativeViewManager('ExpoImage');

Underneath, that is three steps:

1. `globalThis.expo.getViewConfig('ExpoImage')` -- a function native code
   provides -- returns `{validAttributes, directEventTypes}`.
2. React Native's `createViewConfig` merges that with the base view config, so
   style, layout and touch props come free.
3. The result is registered as an ordinary React Native component named
   `ViewManagerAdapter_ExpoImage`.

From there it is a Fabric component like any other, and none of the rest is
Expo-specific: a name, a props class, a shadow node, a descriptor in each
platform's registry, and a mounting peer. `core/ExpoImageComponent.h` compiles
whether or not the build has Expo in it, because nothing in it needs Expo.

**The view config is the contract.** React Native filters props against
`validAttributes` in JavaScript, before anything diffs them, so a prop that is
not listed never reaches C++ -- silently. That is phase 26's
`shouldNotifyLoadEvents` again, except that this time both sides are ours, so
the table in `ExpoModules.cpp` and the parsing in `ExpoImageComponent.cpp` have
to be read together or neither is true.

The same goes for events. `directEventTypes` is keyed `topLoad`, not `load`,
because `EventEmitter::normalizeEventType` prefixes what C++ dispatches; the
registration name beside it is what the app passes as `onLoad`.

## What it does and does not draw

`source`, `contentFit` and `tintColor`. expo-image's `contentFit` is CSS
object-fit rather than React Native's `resizeMode`, and every value it has maps
onto one this platform already draws, so it is translated at parse time rather
than carried as a second vocabulary. `scale-down` becomes `contain`: nothing
here can express "never scale up", and contain is the half that matters for an
image bigger than its box, which is the case scale-down exists for.

Placeholder, transition, blurhash, cache policy and the SF Symbol props are
**left out of the view config**, so they are dropped in JavaScript and never
travel. An app using them gets an image without them rather than an error, which
is the honest outcome for a prop whose effect is decorative.

The module half -- `Image.clearMemoryCache()` and friends -- answers honestly
rather than pretending: there is no disk cache here, so `clearDiskCache` reports
false, which is expo-image's own way of saying nothing was cleared.

## The evidence

Mounting is the same code React Native's `<Image>` uses. That was the point of
doing it this way: `applyImage` on both platforms now reads the source and the
fit off whichever props class it has and is identical afterwards, down to the
image loader's cache.

Which left one thing unprovable. A tree dump showed both images as
`texture=1024x1024` whether the fit was right or wrong -- two views the same
size holding the same picture are identical in every line of that dump and
different on screen. So the dump carries the fit now, on both platforms:

    view tag=10 frame=(24,93 120x120) bg=#1a1e28ff texture=1024x1024 fit=contain
    view tag=12 frame=(160,93 120x120) bg=#1a1e28ff texture=1024x1024 fit=cover
    view tag=14 frame=(296,93 120x120) bg=#1a1e28ff

Three `<Image>`s from expo-image: two that loaded with different fits, and one
pointing at a file that does not exist, which has no texture and whose `onError`
reported `could not open /nope/missing.png`. `onLoad` reported 1024x1024 --
expo-image's payload shape, `{source: {url, width, height}}`, not React
Native's. The two hosts produce the same tree, text line heights aside.

Thirteen of fifteen libraries now. Reanimated and gesture-handler are left, and
both are native libraries in their own right rather than Expo modules.
