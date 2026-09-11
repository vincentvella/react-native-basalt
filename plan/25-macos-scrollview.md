# Phase 25 — `<ScrollView>` on macOS

> **Done, 2026-09-11.** Wheel, touchpad, `scrollTo`, `onScroll`, and state.
> Both hosts produce a byte-identical tree, `scroll=(0,530)` included.

Almost every real screen scrolls, and `VirtualizedList` -- which is `FlatList`,
`SectionList` and most of what an app renders -- is a ScrollView with an
`onScroll` handler. Without one, macOS could show a screenful and nothing more.

## Not an NSScrollView

The obvious implementation is the wrong one. `NSScrollView` wants to own its
document view's size and brings a clip view, scrollers and elasticity with it;
this platform's whole invariant is that React Native decides sizes and the
platform only places. Yoga has already laid the content out at its full height
by the time a mutation arrives -- `overflow: scroll` is what lets it exceed the
viewport -- so the only thing missing is the offset.

That offset is `bounds.origin` on the ScrollView's own view. AppKit then shifts
drawing, clipping and hit testing together, the frames the mounting manager
wrote stay exactly the ones Yoga produced, and nothing has to be undone on the
next mutation. The GTK side reaches the same place by shifting children in its
layout manager, for the same reason.

Hit testing follows the scroll without knowing what a ScrollView is: the point
is translated into bounds space on the way down, and a scrolled view has a
non-zero bounds origin. Getting that wrong gives a list that scrolls correctly
and whose rows answer presses meant for the row above -- so there is a test for
it.

## The wheel

`scrollWheel:` on `RnAppKitView`, forwarded to a handler set only on
ScrollViews. A view with none calls `super`, which walks the responder chain --
so a wheel over a plain row scrolls the list containing it, and a list inside a
list scrolls the inner one first. That is AppKit's own nesting behaviour, got for
free by not intercepting it.

Two details that would each be a silent bug.

**Lines are not pixels.** A touchpad reports pixels and a wheel reports line
counts, both as small numbers, so the deltas cannot be told apart --
`hasPreciseScrollingDeltas` is the only thing that can. Treating a line as a
pixel makes the wheel move content by one pixel a notch, which reads as the
wheel not working. A line is worth 53 pixels here, which is the same constant
the GTK side uses: one notch moves a list by the same amount on both desktops.
Matching the other platform matters more than matching either toolkit's own
default, which is this project's whole argument in one number.

**AppKit's positive Y is a scroll up**, which moves content down and *reduces*
contentOffset. React Native's offset grows downward, like GTK's delta, so both
axes are inverted. `scrollingDeltaY` already accounts for the user's
natural-scrolling preference, so nothing else has to.

Momentum begins no drag. It arrives after the fingers left, as movement with no
drag around it, and reporting a drag that begins and never ends is worse than
reporting none.

## Two things on every scroll, and they are not the same thing

`onScroll` goes to JavaScript, throttled by `scrollEventThrottle`. Without it
`VirtualizedList` never renders past its first window.

`contentOffset` is written back into `ScrollViewState`, **unthrottled**.
`ScrollViewShadowNode::getContentOriginOffset` reads it, and through that so do
`measure`, `measureLayout`, C++ hit testing and view culling. Throttling it
would leave all of those reporting a stale scroll position.

That distinction is not about a toolkit, and neither is the state update's shape
-- returning null to cancel when nothing changed, because the callback can run
more than once when commits race. Both are copied from the GTK file deliberately.

## Commands

`scrollTo` and `scrollToEnd` arrive through `dispatchCommand`, which until now
logged and did nothing on macOS. It runs on the JS thread inside the rendering
update, so it takes the same trip to the main queue that `executeMount` does --
and at the same priority, so a `scrollTo` stays behind the transaction that
created the ScrollView it names. Arriving first would find no entry and be
dropped.

`js/scroll.js` scrolls itself once through a ref, which is how both hosts can be
driven identically from one file: a wheel has to be injected per platform, and a
command does not. It is also the only test of the command path.

## Checking it

`BASALT_TEST_SCROLL="x,y,lines"` builds a real `NSEvent` from
`CGEventCreateScrollWheelEvent` and hands it to the view this project's own hit
test finds. Five lines scrolled to 265 and five more to 530 -- 53 a notch,
exactly -- with `onScroll` reaching JavaScript both times. The wheel landed on
the *content* view and reached the ScrollView through `[super scrollWheel:]`,
which is the responder chain doing its job.

`scripts/compare_hosts.sh` on `js/scroll.js` produces byte-identical trees
including `scroll=(0,530)`, so the command path, the clamp, the state write and
the offset all agree across two desktops.

## What is missing

**No scrollbars.** AppKit's are `NSScroller`, which comes with `NSScrollView`,
so drawing an overlay indicator is real work rather than a property. The GTK
side gets them from its widget theme, which is the one place that platform is
ahead here.

**No momentum or elasticity.** A drag ends where it stopped, with a zero
velocity and `targetContentOffset` equal to the current offset -- the same
honest approximation the GTK side makes. A trackpad flick therefore stops dead,
which is visibly un-Mac-like and is the first thing worth fixing.

**No `contentInset` or `scrollIndicatorInsets` beyond being reported** in the
scroll event. They are read from props and passed through; nothing positions
against them.

**`scrollTo` does not animate.** `animated: true` is applied at once, which is
what `animated: false` asks for -- again matching GTK.

**No zoom.** `zoomScale` is reported as 1 and nothing changes it. Pinch-to-zoom
has no implementation on either desktop.
