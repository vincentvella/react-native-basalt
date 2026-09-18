# ScrollView

Part of the [backlog](../backlog.md). Not scheduled.

**Open (4):**

1. Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11
2. No scrollbars are drawn
3. contentBoundingRect
4. disableViewCulling is never set, which will matter once AT-SPI lands

- Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11.
- ~~No momentum.~~ See the Input section. What is left is Windows, which has no
  fling velocity to model one from, and `onScrollEndDrag`'s velocity, which is
  still reported as zero on every host -- so `ScrollView._isAnimating()` is
  still wrong.
- ~~**`animated: true` scrolls instantly.**~~ Done on all three:
  `core/ScrollAnimation.h` is a cubic ease-in-out over a fixed duration, which a
  fling deliberately is not -- a fling has velocity and no target, this has a
  target and no velocity, and exponential friction approaches a destination
  without ever arriving at it.

  Each host steps it with what it already has: GTK a frame-clock tick callback
  beside the fling's, AppKit a `CADisplayLink` per animating view because the
  system does its own deceleration and there was no stepper to borrow, Windows a
  thread timer because the scroll manager has a tag rather than an HWND. A
  gesture or a wheel cancels it on all three -- the person moving the list wins
  over the app moving it.
- ~~**No snapping or paging.**~~ Done on all three: `pagingEnabled`,
  `snapToInterval`, `snapToOffsets` and `snapToAlignment`, decided once in
  `core/ScrollSnap.h` and settled with the animation next door.

  The rule is "the next point in the direction it was flicked", which is
  symmetric and is what CSS scroll-snap does -- twenty pixels into a page,
  flicked back, the answer is that page's start rather than the one before it.
  Going back two boundaries would let a small flick travel further than a large
  one.

  A snapping list does not coast: the settle replaces the fling, so the velocity
  decides *which* point rather than how far. On Windows there is never a flick
  to replace, because a wheel supplies no velocity.

  What is left here is `maintainVisibleContentPosition`, which is a different
  thing -- keeping the offset stable while content is inserted above it.
- No scrollbars are drawn.
- `contentBoundingRect.origin` is assumed to be zero; iOS positions its
  container view at that origin.
- `disableViewCulling` is never set, which will matter once AT-SPI lands.
