# 37 — gesture-handler, written rather than compiled

Of the fifteen libraries phase 34 measured, two were left, and neither is an
Expo module. react-native-gesture-handler is the smaller one and the harder to
get from anywhere else: it ships Objective-C for Apple, Kotlin for Android and a
TypeScript engine for the web, and **no portable C++ at all**. There is nothing
to compile the way expo-modules-core's `common/cpp` compiles.

What is portable is its *contract*, and the contract is small: eight module
methods, a six-value state machine, and two device events. That is what this
phase implements.

## Where the line falls

Everything in `core/Gestures.h` is platform-independent. A handler is a state
machine fed a pointer position and a list of the views under it, and what it
produces is a payload for JavaScript. The toolkits contribute two things and no
more: where the pointer is, and which views are under it -- which is what each
one's touch dispatcher already works out for React Native's own touches.

The only new thing either platform had to grow is a hit *chain* rather than a
hit *target*. A touch is reported against one view; a gesture may be attached to
any ancestor of the view that was hit, so the recognisers need the whole path,
innermost first, each with its origin in root coordinates. The origin is what
turns a root-relative pointer into the view-relative `x`/`y` a payload carries,
and both toolkits already know how to compute one (`convertPoint:toView:`,
`gtk_widget_compute_point`) including a scrolled ancestor's offset.

## The module, by hand

`RNGestureHandlerModule` is the first TurboModule here that is not generated.
Every other one derives from a React Native spec, because React Native ships its
specs; a third-party library's spec is generated during *its* build against *an
app's* node_modules, and these hosts are generic binaries that are not built per
app. So the eight methods are declared against `TurboModule` directly, from the
same TypeScript interface codegen would have read.

It is registered unconditionally, like every core module. An app without
gesture-handler never looks the name up.

The module does almost nothing: it converts arguments, hops to the UI thread and
calls the registry. The hop is the point. The registry is UI-thread state --
input arrives there and the recognisers run there -- and the module is called
from JavaScript, so rather than lock the registry its callers change threads,
which is the same rule the mounting managers keep and assert. `postToUiThread`
and `postDelayed` joined `core/PlatformServices.h` for it; the second is what a
long press is made of, because "nothing happened for half a second" cannot be
expressed by reacting to input.

## What is recognised

Tap, long press, pan, fling, native and manual. Pinch, rotation and force touch
are created and never activate: they need a second finger or a pressure reading
that a mouse does not have. A test pins that -- BEGAN then FAILED, never ACTIVE
-- because a handler that looks attached and quietly never fires is the worst
possible answer, and "your `onBegin` runs and your `onStart` does not" is at
least legible.

One pointer, always. A desktop has one cursor, so `numberOfPointers` is 1 and a
gesture configured to need two never activates.

Conflicts are the useful half of RNGH's resolution: a handler that activates
cancels every other handler tracking the same pointer unless the two were
declared simultaneous, and `waitFor` holds a handler at BEGAN until the one it
waits for has failed. What is missing is the rest of RNGH's relation graph --
`blocksHandlers`, and the cross-component `waitFor` that resolves only after a
handler in another detector fails.

## Yielding the pointer

A gesture that activates has to take the pointer away from React Native's
responder system, or panning across a `<Pressable>` pans *and* presses it. RNGH
does that with `setJSResponder` on the platforms it was written for. Here both
sides are fed from the same place, so it is simpler: the dispatcher asks the
registry whether anything activated and, if so, cancels the touch it was
reporting.

## The evidence

Thirteen tests in the shared suite drive the registry directly -- the module
hops to a UI thread a test binary does not have -- covering activation,
cumulative translation, the slop and duration limits, ancestor attachment,
conflict resolution, simultaneous handlers, and the gestures that cannot happen
here.

And then the real library, in a real Expo app, on both hosts. Injected input
had to grow for it: a tap cannot exercise a pan, which is defined by the
movement between press and release, so `BASALT_TEST_DRAG` joined
`BASALT_TEST_TAP` on both hosts -- one press, twenty moves, one release.

    gh tap
    gh tap end
    gh pan begin
    gh pan start
    gh pan 11,3
    ... 
    gh pan 220,60
    gh pan end

220 by 60 is exactly the drag that was injected, which is the thing a
translation can most easily get wrong: RNGH's is cumulative from where the
gesture began, and an app that adds up per-frame deltas moves twice as far as
the pointer did. macOS and Linux produce the same lines.

## One thing that is not ours

RNGH calls `UIManager.getViewManagerConfig('getConstants')` at import, and the
bridgeless UIManager answers every such call with a soft error on the console.
It happens on every new-architecture platform, is RNGH's own call, and is
harmless. Worth knowing before someone goes looking for it here.
