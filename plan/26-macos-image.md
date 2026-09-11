# Phase 26 — `<Image>` on macOS

> **Done, 2026-09-11.** All four resize modes, file, `data:` and http sources,
> and a load-event bug fixed on both desktops.

This is the last of the four components an ordinary app is built from. macOS now
mounts `<View>`, `<Text>`, `<ScrollView>` and `<Image>`; the only one left is
`<TextInput>`.

## The half that was never platform-specific

React Native's cxx `ImageManager` is a stub that never produces an
`ImageResponse`, so `ImageState` never carries anything and the platform view is
expected to load its own image -- which is what Android does too, from
`ReactImageView` rather than from the shadow node. So the mounting manager reads
the URI off `ImageProps::sources` and asks a loader for pixels.

Writing that loader a second time made something visible that was not visible
the first time: **the fetch is not platform-specific at all**. `file://`, a bare
path, `data:` and `http(s)` mean the same things on every desktop, and only the
decode differs. So `core/ImageBytes.cpp` is that fetch, shared, and the GTK
loader now uses it too -- losing its glib dependencies for base64, file reading
and URI unescaping in the process, all of which had to be written in plain C++
to live in core.

That is the phase-17 pattern arriving on schedule: the core grows when a second
platform shows which half of something was general. It was not obvious from one
implementation, and it was obvious from two.

## What did differ

**A CGImage is immutable and thread-safe**, so the decode happens on the worker
thread. The GTK side has to decode on the main thread because a `GdkTexture` is
a GObject and constructing one is only safe on the thread that will use it. The
AppKit loader therefore does strictly less on the main thread than its
counterpart, which is the first place these two have diverged in the macOS
platform's favour.

**ImageIO, not NSImage.** An `NSImage` is a list of representations at different
sizes with a resolution attached, and asking one for a CGImage means telling it
a size and a context -- so a 160x100 PNG can come back 320x200 on a Retina
display and then measure wrong. `CGImageSource` hands back exactly what is in
the file.

**CGImage draws bottom-up** and these views are flipped, so the draw flips the
context. Without it every photograph comes out upside down, which reads as a
broken decoder rather than a coordinate system.

The resize-mode arithmetic is duplicated from the GTK side rather than shared,
because the two view layers link no common code at all -- that is what keeps
each testable without React Native. Duplication with a comment saying so beat
giving the view layers a shared dependency.

## A bug in both platforms, found by looking

`onLoad` and `onError` never fired, on either desktop. The tree was right, the
pixels were right, and an app asking to be told about a failure was told
nothing.

The cause is worth writing down in full, because it is a shape that will recur.
`ImageProps::shouldNotifyLoadEvents` is Android's signal that handlers exist.
`Image.android.js` sets it -- and that *is* the `Image.js` this platform
resolves to, through the shim redirect in `metro-config.js`. But
`ImageViewNativeComponent`'s `__INTERNAL_VIEW_CONFIG` branches on
`Platform.OS === 'android'`, and a platform that is neither iOS nor Android
takes the **iOS** branch, whose `validAttributes` has no
`shouldNotifyLoadEvents` in it -- because iOS does not use one. So the prop was
filtered out of the props before it ever reached C++.

This is the shim policy meeting a file that branches internally: we take
Android's `Image.js` and get iOS's view config, and the two disagree about how
load events are announced.

The fix is to do what iOS does: emit unconditionally and let the emitter be the
thing that knows whether anybody is listening -- `eventEmitterForTag` already
returns null when nothing is. One line in each mounting manager, rather than
forking a two-hundred-line view config to change one ternary. `onError` now
fires on both desktops with the same message.

Worth noting what found it: not a test and not a crash, but writing an app that
deliberately pointed at a file that does not exist and checking that the failure
was *reported* rather than merely survived.

## Where the two hosts now stand

`js/image.js` renders four resize modes, an inline `data:` PNG and a broken
source. With frames ignored, the GTK and AppKit trees differ by exactly one
thing -- `role=img`, which macOS does not emit because it has no accessibility.

That is now the *only* difference on both the text app and the image app.
Accessibility has gone from "a gap" to "the gap", and it is measurable:
`compare_hosts.sh` passing on every app is a definition of done for it.

## What is missing

**No animated images.** `CGImageSourceCreateImageAtIndex(source, 0, ...)` takes
the first frame, so an animated GIF is a still. An animator needs a frame clock
per image and a place to keep the decode state.

**Nothing evicts from the cache.** Decoded images are kept for the life of the
process, keyed by URI, on both desktops.

**No `defaultSource`, `loadingIndicatorSource`, `blurRadius`, `tintColor` or
`capInsets`.** All are read into `ImageProps` and ignored.

**No `onProgress`**, which would need the fetch to report bytes as they arrive
rather than returning a string at the end.

**`resizeMode: 'repeat'` centres instead of tiling**, as on GTK.
