# Image

Part of the [backlog](../backlog.md). Not scheduled.

**Open (7):**

1. Nothing evicts the texture cache
2. resizeMode: 'repeat' falls back to center; a repeating draw needs a pattern no
3. blurRadius, overlayColor, fadeDuration and progressiveRenderingEnabled are ign
4. Assets are never fetched over the network, so a dev server's assets do not wor
5. Nothing caches a downloaded asset, which is right for a local file and will no
6. onProgress and onPartialLoad are never emitted
7. IImageLoader itself is still unimplemented, so Image

- Nothing evicts the texture cache. A long-lived app that scrolls through many
  remote images grows without bound.
- `resizeMode: 'repeat'` falls back to `center`; a repeating draw needs a
  pattern node rather than one texture append.
- ~~**`tintColor` is ignored.**~~ Done on all three, and on expo-image's
  `tintColor` too. The image becomes a stencil and the colour is what is drawn,
  which is what the prop means: recolour the silhouette rather than blend with
  the pixels. Each toolkit spells that differently -- a GskMaskNode in alpha
  mode on GTK, `CGContextClipToMask` on AppKit, `FillOpacityMask` on Direct2D,
  which needs aliased antialiasing and refuses the call without it.

  The tree dump reports `tint=#rrggbbaa`, in one format on all three, which is
  what makes a paint property assertable on hosts that have no rendering
  assertions: cross-host parity now compares the tint the way it compares the
  fit.

- `blurRadius`, `overlayColor`, `fadeDuration` and
  `progressiveRenderingEnabled` are ignored.
- ~~A `require()`d image drew nothing.~~ It laid out at the right size and had
  no pixels, and the reason was neither the loader nor the mounting manager:
  Metro's `build` command has no `--assets-dest`, so the files were never copied
  next to the bundle. `scripts/copy_assets.js` reads the asset descriptors back
  out of the bundle Metro just wrote and copies each one to where
  `AssetSourceResolver.scaledAssetURLNearBundle` will look for it -- including
  that rule's own escaping, where each `../` becomes a single `_`. The demo's
  images never showed this because they are `{uri: ...}` rather than requires.
- Assets are never fetched over the network, so a dev server's assets do not
  work; `downloadAsync` rejects saying so. The host has an http client already.
- Nothing caches a downloaded asset, which is right for a local file and will
  not be for a remote one.
- `onProgress` and `onPartialLoad` are never emitted.
- `IImageLoader` itself is still unimplemented, so `Image.getSize` and
  `Image.prefetch` do nothing. That is a separate seam from the rendering path.
