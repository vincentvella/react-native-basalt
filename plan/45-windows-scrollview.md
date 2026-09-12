# Phase 45 — `<ScrollView>` on Windows

> **Done, 2026-09-12.** Wheel, `scrollTo`, `onScroll`, and state. `js/scroll.js`
> runs on all three desktops now.

    [js] scrolled to 530          <- scrollTo through a ref
    BASALT_TEST_SCROLL: 5 lines at (450, 350)
    [js] scrolled to 795          <- 530 + 5 x 53
    BASALT_TEST_SCROLL: -3 lines at (450, 350)
    [js] scrolled to 636          <- 795 - 3 x 53
    BASALT_TEST_SCROLL: -99 lines at (450, 350)
    [js] scrolled to 0            <- clamped

    view tag=54 frame=(24,24 852x652) bg=#2b3445ff clip scroll=(0,1060)
      view tag=52 frame=(0,0 852x1960)

Fifty-three pixels a notch, which is the number the other two desktops use, and
`scroll=(0,1060)` printed in the same shape `describeTree` prints on macOS.

## Almost all of it is the GTK file

Yoga does the hard part. A ScrollView's node carries `overflow: scroll`, which
lets its child exceed the viewport instead of being clamped to it, so by the time
a mutation arrives the content is already laid out at full size and the only
thing missing is an offset. What is left -- the clamp, the throttle, the state
write-back, the commands -- is arithmetic, and arithmetic is not a toolkit
question. `Win32ScrollView.cpp` is `GtkScrollView.cpp` with different plumbing at
two ends, deliberately, because the ways this goes subtly wrong are shared:

**Two things happen on every scroll and they are not the same thing.** `onScroll`
goes to JavaScript, throttled by `scrollEventThrottle`; without it
`VirtualizedList` never renders past its first window. `contentOffset` is written
into `ScrollViewState`, **unthrottled**, because
`ScrollViewShadowNode::getContentOriginOffset` reads it and through that so do
`measure`, `measureLayout`, C++ hit testing and view culling. Throttling the
second would leave all of those reporting a stale position.

**The state update returns null when nothing changed.** The callback can run more
than once when commits race, so it has to stay a pure function of `oldData`.

**`zoomScale` is set to 1 explicitly.** `ScrollEvent` initialises it to 0 and
`VirtualizedList` only repairs *negative* values, so leaving the default makes
every list measurement come out as zero.

## Routing, which is this platform's alone

GTK attaches a `GtkEventControllerScroll` per widget; AppKit walks the responder
chain from the view the wheel landed on. Both get "a wheel over a row scrolls the
list containing it, and a list inside a list scrolls the inner one first" for
free. A Win32 view has no window of its own and there is no chain to walk, so
`scrollAt` does it by hand: hit test for the view under the pointer, then walk up
parents offering the wheel to each ScrollView until one takes it.

Taking it means "is a ScrollView and is enabled", not "moved" -- matching AppKit.
A list already at its bottom still eats the wheel rather than handing it to the
list behind it, which is what stops a nested list from dragging its parent along
at the end of every scroll.

## Two ways to get a wheel wrong on Windows

**`WM_MOUSEWHEEL` carries screen coordinates.** Every other mouse message is
client-relative. This one is not, because it is sent to the *focused* window
rather than the one under the pointer, so a client-relative position would be
meaningless. Forgetting `ScreenToClient` gives a wheel that scrolls the wrong
list, or nothing, depending on where the window sits on the desktop -- and it
looks correct as long as the window is at the top-left of the screen.

**Both axes are inverted, for different reasons.** A positive `WM_MOUSEWHEEL`
delta is a turn away from the user, which moves content down and so *reduces*
`contentOffset`. A positive `WM_MOUSEHWHEEL` delta is a tilt to the right, which
is the direction `contentOffset` already grows in. So one is negated and the
other is not, which reads like a mistake and is not.

Because those are the two, `BASALT_TEST_SCROLL` does not enter at the manager the
way `BASALT_TEST_TAP` enters at the dispatcher. It converts its point back out to
screen coordinates and `SendMessage`s a real `WM_MOUSEWHEEL` to the real window
procedure, so the hatch exercises both and skips only the physical device.

## Drag phases, which nothing here reports

GTK's controller emits `scroll-begin` and `scroll-end`; AppKit's `NSEvent`
carries a phase. `WM_MOUSEWHEEL` is a bare notch with nothing around it. So a run
of notches is treated as one drag and ended by a 150ms idle timeout: without it
there would be no `onScrollBeginDrag` or `onScrollEndDrag` on this platform at
all, and the components that wait for one would wait forever.

The timer is `postDelayed`, which has no cancel, so two things guard it. A
generation counter per entry, so a timer that wakes to find another notch arrived
after it was scheduled leaves the drag alone. And a shared alive flag, so a
surface torn down inside those 150ms does not leave a timer calling through a
dangling `this` -- the same arrangement `Win32ImageLoader` already uses.

## Fourteen tests, which neither other platform has

GTK's and AppKit's scroll managers are checked end to end by
`scripts/compare_hosts.sh` running `js/scroll.js` on both, which is a better test
in every way except that it needs two working desktops. This machine has one:
WSL2 will not start here because virtualisation is disabled in firmware. So the
arithmetic is tested directly instead -- the clamp at both ends, the adoption of
an offset already in the state, the re-clamp when content shrinks under a
scrolled offset, the innermost-first walk, `scrollEnabled: false` passing the
wheel on, the commands, and hit testing following the scroll.

Two things stay unobservable from a test and are worth naming rather than working
around. A `ShadowView` built by hand has no `ShadowNodeFamily`, so
`State::updateState` finds nothing to commit into and the unthrottled write-back
cannot be asserted on. An `EventEmitter` built by hand has no `EventDispatcher`,
so `onScroll` goes nowhere. Both are covered by running `js/scroll.js`, which is
what the transcript at the top is.

## What is missing

**No scrollbars.** Not a property anywhere: drawing an indicator is real work on
this platform, as it is on macOS. GTK gets them from its widget theme and remains
the only one of the three that has them.

**No momentum, no elasticity, no kinetic scrolling.** A wheel stops where it
stopped. `onScrollEndDrag` reports a zero velocity and a `targetContentOffset`
equal to the current offset, which is the same honest approximation both other
desktops make.

**`SPI_GETWHEELSCROLLLINES` is ignored.** Honouring the user's lines-per-notch
setting is the more native thing and would make one notch move a different
distance here than on the other two desktops. This is the trade this project
keeps making the other way. The setting's "one screen at a time" value has no
implementation either way.

**No precision touchpad.** A Windows precision touchpad reports through
`WM_POINTER*`, not `WM_MOUSEWHEEL`, so a two-finger scroll arrives as notches
like a wheel rather than as pixels. AppKit tells the two apart with
`hasPreciseScrollingDeltas` and GTK with `GDK_SCROLL_UNIT_WHEEL`; there is no
equivalent here until the pointer messages are handled.

**No `contentInset` beyond being reported** in the scroll event, and **no zoom** --
both the same as macOS.
