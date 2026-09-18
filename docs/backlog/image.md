# Image

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. resizeMode: 'repeat' falls back to center; a repeating draw needs a pattern no
2. blurRadius, overlayColor, fadeDuration and progressiveRenderingEnabled are ign
3. Assets are never fetched over the network, so a dev server's assets do not wor
4. Nothing caches a downloaded asset, which is right for a local file and will no
5. onProgress and onPartialLoad are never emitted

- ~~Nothing evicts the texture cache.~~ Done on all three, in
  `core/ImageCache.h`. Each host kept decoded images in an `unordered_map`
  nothing removed from, so a long list of remote images, scrolled, held every
  one ever seen until the process exited.

  Least recently *used*, not least recently added, which is the distinction the
  case needs: scrolling a list back up should not re-decode the rows about to
  be on screen again. A hit counts as a use, so it moves an entry to the front.

  The policy is in core and the pixels are not, because the three hosts hold a
  `GdkTexture`, a `CGImage` and an `RnWin32Image` and none of those can be
  named there. Core answers with the URIs to release and never sees one. Each
  host measures its own bytes -- GDK four per pixel, Core Graphics the row
  stride times the height, since rows are padded and width times four would
  undercount.

  An image larger than the whole budget is kept rather than refused: it is on
  screen, and evicting it would decode it again on the next frame, forever.
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
  And nothing is cached: `downloadAsync` returns the file where it lies, which
  is right for a local asset and will not be right for a remote one.
- Nothing caches a downloaded asset, which is right for a local file and will
  not be for a remote one.
- `onProgress` and `onPartialLoad` are never emitted.
- ~~`IImageLoader` itself is still unimplemented, so `Image.getSize` and
  `Image.prefetch` do nothing.~~ Done on all three. Each host's image loader
  now *is* an `IImageLoader`, so a size asked for something already on screen
  is answered from the same cache that is holding its pixels.

  The reason it was unimplemented is upstream and worth knowing before anyone
  looks for the seam: `ReactCxxTurboModuleProvider` constructs
  `ImageLoaderModule(jsInvoker_)` with the default empty `weak_ptr`, and
  nothing in `ReactInstanceConfig` can supply one. So there is no hook to fill
  in -- the module has to be built by the host instead, which works because a
  host's own providers are consulted before the built-in ones. See
  `docs/backlog/upstream.md`.
