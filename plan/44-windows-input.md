# Phase 44 — input on Windows

> **Done, 2026-09-12.** A React app responds to a click on Windows. `js/press.js`
> renders a `<Pressable>`, and pressing it runs `onPress`, re-renders, and paints
> the new tree.

    BASALT_TEST_TAP: tapping (200, 80)
    [js] pressed 1
    BASALT_TEST_TAP: tapping (200, 80)
    [js] pressed 2
    BASALT_TEST_TAP: tapping (200, 80)
    [js] pressed 3

    view tag=1 frame=(0,0 900x700)
      view tag=8  frame=(0,0 900x700)  bg=#1f2129ff
      view tag=4  frame=(24,24 852x120) bg=#4d8cf2ff
        view tag=2  frame=(394,28 64x64) bg=#e6eeffff
      view tag=12 frame=(24,168 40x40)  bg=#59cc8cff
      view tag=14 frame=(76,168 40x40)  bg=#59cc8cff
      view tag=16 frame=(128,168 40x40) bg=#59cc8cff

Three pips for three presses, which is the whole loop and not only the part this
phase wrote: hit test, touch, the responder system's negotiation in JavaScript,
Pressability, `setState`, React re-rendering, Fabric diffing, the mutations
coming back down, and Direct2D painting them.

## The dispatcher is a copy, deliberately

`Win32TouchDispatcher` is the same file as `AppKitTouchDispatcher` and
`GtkTouchDispatcher` down to the state machine and the event construction. That
is not laziness about a third platform: the part of this that is easy to get
subtly different is not about the toolkit at all.

Two things in particular. Which touches go in `event.touches` versus
`event.changedTouches` -- on a touchend the finger is *gone*, so it must appear
only in `changedTouches`, and getting that wrong leaves the responder system
believing a finger is still down and swallows every press after the first. And
which view a gesture is reported against -- every touch in a gesture goes to the
target it *began* on, even after the pointer leaves that view. Neither is
something Win32 has an opinion about, and if the three files ever diverge an
app's `onPress` will fire on two desktops and not the third.

## What Win32 does not give you

Both other platforms hand the dispatcher two things this one had to write.

**Picking.** GTK has `gtk_widget_pick` and AppKit has `-hitTest:`, because on
those platforms a React Native view really is a widget. A Win32 view is a plain
C++ object with no window of its own, so all of hit testing is this project's.
It was already written and tested in phase 39 -- `hitTest` in `RnWin32View.h`,
twelve tests, including a transform inversion neither other platform does -- so
this phase did not have to touch it. Writing hit testing a phase before anything
could press anything turned out to be worth it.

**Capture.** A drag that leaves the window stops being reported and the release
never arrives, which leaves the responder system waiting for a finger that is
gone. AppKit and GTK route a drag back to the widget that took the press for
free; here `hostProc` calls `SetCapture` on the press and `ReleaseCapture` on
the release.

The order in `WM_LBUTTONUP` matters and is not obvious. `ReleaseCapture` sends
`WM_CAPTURECHANGED` *synchronously*, and `WM_CAPTURECHANGED` is a cancel --
capture taken away by a modal dialog or by Alt+Tab means the release will never
come. Releasing before dispatching the end would therefore turn every ordinary
click into a cancelled touch, which is a press that never fires. So the end goes
first, and by the time `WM_CAPTURECHANGED` arrives there is no touch left to
cancel.

## The hit chain, which is the gesture half

React Native's own touches need one tag. `react-native-gesture-handler` needs
the whole chain of views under the pointer, innermost first, each with its origin
in the surface root's coordinates -- because a handler may be attached to any
ancestor of the view that was actually hit, and its payload reports positions
relative to *its* view.

AppKit gets those origins from `convertPoint:toView:`. There is no toolkit
geometry here, so the composition is written out: carry a point up the chain,
through each view's `localToParent` and out of each parent's scroll offset. That
is exactly the mirror of what `hitTest` does on the way down, which is the only
thing that makes the two agree about a rotated or scrolled ancestor. Getting it
wrong would put a gesture's `x`/`y` somewhere the press was not, on the same
press the touch reported correctly.

It is built only when something is attached. An app that does not use RNGH must
not pay to walk the tree twice per press.

## Eight tests, and what they cannot cover

`tests/test_win32_input.cpp` drives the dispatcher against a real mounted tree:
the tap finds the tag the mounting manager registered, a move with no button
down is dropped as hover, a press outside the surface does not arm the state the
host reads to decide about capture, a pan attached to an ancestor of the pressed
view still activates, a scrolled ancestor does not move the chain out from under
it, and a native handler that activates on contact takes the pointer away.

What none of them cover is that Windows routes `WM_LBUTTONDOWN` to `hostProc` at
all. They enter at `dispatchTouch*`, one level above the window procedure,
because synthesising a real click on Windows means `SendInput`, which moves the
actual cursor and so cannot run beside anything else on the machine. That is the
same objection CGEvent raises on macOS, and the same hole `BASALT_TEST_TAP`
leaves on all three platforms: a person clicking the window is still the only
check on the last inch.

## What is still missing

Everything a mouse has that a finger does not. No hover, so `onMouseEnter` never
fires. No right button. And no keyboard, which arrives with `<TextInput>` rather
than before it, because there is nothing yet that focus could belong to.

The wheel is the exception, and it went to phase 45 rather than here. A touch
dispatcher is the wrong owner for it: what routing a wheel needs is the set of
tags that are ScrollViews, which is the mounting manager's to know.
