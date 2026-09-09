# Phase 6 — `<Image>` and `<ScrollView>`

> **Done, 2026-09-09.** `src/GtkImageLoader.*`, `src/GtkScrollView.*`, the
> descriptors in `LinuxComponentRegistry.h`, and clipping plus a scroll offset
> in `RnView`.

Two components, two different shapes of problem.

## Image

The interesting part was finding out that React Native does not hand a C++ host
any pixels. `ImageManager`'s cxx variant is a stub, so `ImageState` is always
empty and the platform view loads its own image -- which is what Android does.
`GtkImageLoader` reads the URI off the props and produces a `GdkTexture`. See
`plan/decisions.md`.

## ScrollView

Yoga does the layout; the platform supplies an offset, clipping, and two
write-backs per scroll. The child structure is the thing to know: one content
child, not N.

`ScrollViewComponentDescriptor` is a bare alias, unlike `ImageComponentDescriptor`
which needs an `ImageManager` in the ContextContainer. Leaving ScrollView out of
the registry does not fail loudly -- the registry substitutes
`UnimplementedNativeView`, which has no `ScrollViewState`, and the symptom is a
ScrollView that renders but never scrolls.

## Side effect

`overflow: 'hidden'` came free with the clipping ScrollView needed, and is now
wired from `BaseViewProps::getClipsContentToBounds()`.

## Not done

Momentum, snapping, paging, scrollbars, animated scrolling, and the
`IImageLoader` seam behind `Image.getSize`. See `plan/backlog.md`.
