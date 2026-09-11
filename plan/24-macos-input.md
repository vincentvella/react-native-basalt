# Phase 24 — input on macOS

> **Done, 2026-09-11.** `<Pressable>` works. Three clicks on one desktop and
> three on the other produce a byte-identical tree.

macOS could render and could not be touched. The event beat had been running
since phase 20 with nothing to deliver.

## What the touch model actually is

React Native's `Pressability` -- what backs `<Pressable>`, `<TouchableOpacity>`
and every `onPress` in an app -- runs on the responder system in JavaScript, and
the responder system is fed by `touchstart` / `touchmove` / `touchend`. So a
desktop pointer is reported as a single touch point, which is also what React
Native for Windows and macOS do. W3C pointer events exist alongside these and
are only consulted for hover, behind a feature flag.

`AppKitTouchDispatcher` is therefore the same state machine as
`GtkTouchDispatcher`, deliberately down to the event construction: which touches
are in `touches` versus `changedTouches`, that a gesture is reported against the
view it *started* on, that `touches` is empty on touchend. None of that is about
a toolkit, and if the two diverge an app's `onPress` fires on one desktop and
not the other.

The parts that had to be written fresh are the two the toolkits differ in: how a
mouse event reaches us, and how to find what is under it.

## Getting the mouse

AppKit delivers a mouse event to the view *its* hit testing picked, which is
some view deep in the tree. React Native wants the event against the surface
root, hit-tested React Native's way. GTK gets this for free by attaching
controllers to the root.

The answer is that every `RnAppKitView` implements `mouseDown:` and forwards,
and the root is where the forwarding stops: a view with no input handler walks
up its superviews until it finds one. The dispatcher sets a handler on the root
and nowhere else.

The obvious alternative -- override `hitTest:` so the root swallows everything
-- was rejected. It would also swallow cursor rectangles, tooltips and tracking
areas, which `<TextInput>` and every hover affordance will need, and AppKit's
own hit testing is doing no harm in the meantime.

The root holds its handler weakly and the dispatcher owns the trampoline object,
so a destroyed dispatcher leaves no dangling handler. That is tested, and the
test needed its own `@autoreleasepool` to be true: a weak reference is zeroed in
`dealloc`, and reading one retains and autoreleases the result, so the
assertion only means anything once the pool that saw those has drained. It
failed once for exactly that reason, which is a better way to learn it than the
alternative.

## Finding what is under it

`RnAppKitHitTest` is a free function in the *view* layer, not in the dispatcher,
so testing it needs no React Native -- and hit testing is the part of input most
likely to be quietly wrong. It walks subviews back to front, because AppKit's
array is front to back and a hit test wants the view drawn on top.

Six tests, and the one worth naming is `hit_test_uses_top_left_coordinates`: an
unflipped hit test finds a child 30 from the *bottom* instead of the top, which
is the same bug `isFlipped` exists to prevent and is invisible in a vertically
symmetric layout.

A child is only entered when the point is inside its frame, so a child
overflowing its parent -- React Native's default, since `overflow` is `visible`
-- is not reachable through that parent. In practice Fabric's view flattening
hoists most such children out to an ancestor that does contain them. The GTK
side has the same limit for the same reason.

## Two ways to fake a click, and why both

`BASALT_TEST_TAP` calls into the dispatcher directly. It proves hit testing,
emitter lookup, the event beat and everything above it -- and proves nothing at
all about whether AppKit routes a click to these views.

`BASALT_TEST_CLICK` posts real `NSEvent`s to the window through `NSApp
postEvent:`, which exercises NSView's hit testing, the responder chain and
`mouseDown:`/`mouseUp:`. It is in-process, so it needs none of the accessibility
permission `CGEvent` would -- which is what makes it usable in automation where
a genuine click is not.

Having only the first would have left the most platform-specific part of this
untested. It is also how the GTK side is arranged, where the end-to-end suite
uses xdotool to go through GDK and the synthesised tap to go around it.

## What it gets to

`js/press.js` is a `<Pressable>` whose press count is rendered as a row of
boxes, so the same file proves the same thing on a platform with no text engine.
Three real clicks produce three boxes.

`scripts/compare_hosts.sh` now forwards `BASALT_COMPARE_TAP` to both hosts --
they read the same variable, which is one of the things a single env-var prefix
bought -- and on this app the two trees are byte-identical after three taps.
That is the entire input path compared across two desktops: hit testing, emitter
lookup, the beat, the responder negotiation, React's re-render, and the
mutations coming back down.

## What is missing

**No keyboard, and no focus.** Nothing is reachable by Tab, and no key event
reaches JavaScript. That arrives with `<TextInput>`, which needs the focus model
anyway.

**No hover.** `mouseMoved:` needs an `NSTrackingArea`, and the touch model has
nowhere to put hover regardless -- it belongs to pointer events, which are
behind a feature flag on both platforms here.

**No scroll wheel**, which is `<ScrollView>`'s problem rather than input's.

**`offsetPoint` is the page point**, not relative to the target view. Honest
rather than correct, and the same approximation the GTK side makes; Pressability
does not read it.

**No gesture cancellation from the platform.** `dispatchTouchCancel` exists and
nothing calls it: AppKit has no equivalent of GTK's gesture `cancel`, and the
case it covers -- a press interrupted by the window losing focus -- has no
handler yet.
