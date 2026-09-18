# ScrollView

Part of the [backlog](../backlog.md). Not scheduled.

**Open (6):**

1. Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11
2. animated: true scrolls instantly
3. No snapping, paging or maintainVisibleContentPosition
4. No scrollbars are drawn
5. contentBoundingRect
6. disableViewCulling is never set, which will matter once AT-SPI lands

- Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11.
- ~~No momentum.~~ See the Input section. What is left is Windows, which has no
  fling velocity to model one from, and `onScrollEndDrag`'s velocity, which is
  still reported as zero on every host -- so `ScrollView._isAnimating()` is
  still wrong.
- `animated: true` scrolls instantly.
- No snapping, paging or `maintainVisibleContentPosition`.
- No scrollbars are drawn.
- `contentBoundingRect.origin` is assumed to be zero; iOS positions its
  container view at that origin.
- `disableViewCulling` is never set, which will matter once AT-SPI lands.
