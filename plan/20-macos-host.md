# Phase 20 — the macOS host

> **Done, 2026-09-10.** Hermes runs a script, Fabric commits, and the result
> appears in an NSWindow. Both hosts produce a byte-identical view tree from the
> same JavaScript.

Phase 19 left a mounting manager with nothing driving it. A host is what drives
it: a `ReactHost`, a JavaScript runtime, an event beat, a frame clock, and a
window. This is the step where macOS stops being a view layer and becomes a
platform.

## What a host has to supply

The list is short and, now that there are two, it is worth reading as a list of
what a third would need:

| Piece | Where macOS's came from |
|---|---|
| `IMountingManager` | `MacMountingManager` — phase 19 |
| `RunLoopObserverManager` | ReactCxxPlatform, plus a platform observer |
| `AnimationChoreographer` | `MacAnimationChoreographer` — new here |
| `ContextContainer` | shared: http and websocket client factories |
| `ComponentRegistryFactory` | `ComponentRegistryMac` — phase 19 |
| `FontRegistry` | `FontRegistryCoreText` — new here |
| Feature flags, TurboModules, bindings | shared with GTK, unchanged |

Three of those seven are shared verbatim, one is a single file each, and the
three that are genuinely new are the run loop, the frame clock and fonts. That
is a fair account of what a desktop host costs once the core exists.

**The event beat.** Nothing an `EventEmitter` produces reaches JavaScript on its
own: `EventQueue::onEnqueue` sets a flag, and the queue is flushed when
something calls `RunLoopObserverManager::onRender()`. React Native asks for
`Activity::BeforeWaiting`, which on iOS is a `CFRunLoopObserver` — so macOS's is
twelve lines and GTK's is the interesting one, since GLib has no such thing and
it has to be built out of a `GSource` that does its work in `prepare()`.

It is added to `kCFRunLoopCommonModes` rather than the default mode. A modal
panel or a live window resize switches modes, and a beat that stopped there
would stop delivering every event JavaScript is waiting on until the user let go
of the mouse.

**The frame clock.** `CADisplayLink`, via `-[NSView displayLinkWithTarget:selector:]`,
which is macOS 14 and later. The alternatives are `CVDisplayLink`, itself
deprecated and calling back on its own thread, and a timer aligned to nothing;
neither is worth carrying for a platform that targets the current Expo. It is
added paused and started by `resume()`, because holding it open would wake the
process at display rate for an app that never animates — the same trade the GTK
side makes by not holding the frame clock open.

`targetTimestamp` rather than `timestamp`: the animation backend wants the time
the frame will be shown, not the time the callback ran.

**Fonts.** The seam phase 17 named, and the first time anything has had to fill
it twice. Core Text registers process-scoped, which is what a bundled font wants
and the only scope needing no consent. The half that is not obvious is the same
half fontconfig has: a font file carries its own family name, usually not the
one the app calls it, so the file's real family is read back with
`CTFontManagerCreateFontDescriptorsFromURL` rather than assumed. Nothing reads
`resolveFontFamily` yet — there is no Core Text text layout — and it is
implemented anyway, because the alternative is a registry that silently records
nothing and a font milestone that begins by debugging this file.

## The result, and how it is checked

`scripts/compare_hosts.sh` runs the same script through both hosts and diffs the
tree each dumped. On `js/demo.js`:

```
view tag=1 frame=(0,0 900x700)
  view tag=101 frame=(24,24 836x376) bg=#9b59f6ff
  view tag=100 frame=(48,48 160x90) bg=#e6eeffff
  view tag=104 frame=(24,416 852x260) bg=#56c98aff
```

Byte-identical. That is the check this whole architecture is for: the same
JavaScript, the same Fabric, the same Yoga, two completely different view layers,
and if the trees ever differ the diff says so before anybody looks at a
screenshot. It needs both toolkits installed, so it runs on a developer's Mac
rather than in CI, where each host only exists on its own side.

`RN_MAC_SNAPSHOT` renders what is actually on screen, which fails differently
from the tree dump: a correct tree can still paint nothing if the layer or the
window is wrong.

## Why the bundle is `js/demo.js` and not a React app

`js/demo.js` drives `nativeFabricUIManager` directly — no React, no
`react-native` JavaScript, no `AppRegistry`. It is the same first light-up the
GTK host had, and it is what macOS can currently render, for two separate
reasons that are worth keeping apart.

The first is that `MacMountingManager` mounts `<View>` and nothing else, so a
React app with any text in it would render blank rectangles.

The second is the one that will outlast the first: **React Native's JavaScript
has no name for this platform.** `Platform.OS` comes from `Platform.ios.js` or
`Platform.android.js`; this project supplies `Platform.linux.js` through a Metro
resolver, and nothing yet supplies a macOS equivalent. Bundling with
`platform=macos` today resolves nothing and fails at the first import. That is a
bundler and JavaScript-layer problem rather than a native one, and it is the
next thing between here and running a real Expo app on a Mac.

## Found on the way

`core/HttpClient.cpp` lives in `rn_desktop_core`, and libcurl was named only on
`rn_mounting`. That worked for exactly as long as the GTK host was the only thing
linking core: the archive member goes unreferenced until something asks for an
http client, so the missing dependency was invisible. The macOS host asked.

This is the same shape as the two seams phase 19 found, and worth naming as a
pattern rather than as three incidents: **a static library's missing dependency
is not an error until a second consumer references the member that needs it.**
The portability probe cannot catch these for the same reason. What catches them
is a second platform, which is the argument for having one.

## What is missing

No input. `GtkTouchDispatcher` has no counterpart, so nothing a user does
reaches JavaScript — the beat is running and has nothing to deliver. No
keyboard, no focus.

No dev mode in practice. `RN_MAC_DEV` is wired through to `ReactInstanceConfig`
exactly as the GTK host does it, and untested, because a Metro bundle for this
platform is the JavaScript-layer problem above.

The host's environment variables are `RN_MAC_*` where GTK's are `RN_LINUX_*`.
Two prefixes for the same knobs is precisely the inconsistency this project
exists to avoid; they should become one. Left as is here because renaming the
Linux ones touches CI and the end-to-end suite, and doing that in the same change
as a new host would hide one in the other.
