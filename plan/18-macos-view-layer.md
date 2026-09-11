# Phase 18 — the macOS view layer

> **Started, 2026-09-10.** An AppKit view layer that paints, with tests and a
> snapshot. No Fabric behind it yet.

Phase 17 argued that the half of this project which is not the view layer costs
a new platform one seam. That argument was made by a linker, which makes it
worth something, but it said nothing at all about the view layer — and the view
layer is where most of the GTK work went. This is the beginning of finding out
what the second one actually costs.

macOS first because it is the one that can be built and looked at on this
machine. Windows is the same shape and cannot be verified here.

## What it is

`native/appkit/RnAppKitView.{h,mm}`, deliberately the same shape as `gtk/RnView`: a
view owns a tag and an absolute frame, does no layout of its own, and places
children at the rects the shadow tree already resolved. Yoga has run by the time
a mutation arrives; a second layout system underneath it is the thing to avoid.

Two decisions carry most of the weight.

**Flipped.** AppKit's origin is bottom-left and React Native's is top-left. An
unflipped view renders a correct layout upside down about its own centre, which
reads as an ordering bug rather than a coordinate one and can survive a
screenshot if the design happens to be near-symmetric. There is a test that
states it in numbers: a child at y=32 in a 420-tall parent is at 32, not 228.

**Layer-backed, with props as layer properties.** Background colour, opacity,
corner radius, clipping and transform all exist on `CALayer` with the semantics
React Native wants. The GTK side composes these by hand in a snapshot function;
here it would take work to avoid them. `wantsLayer` is set in `initWithFrame:`
rather than on demand, because a view that acquires a layer later loses what was
set on it before, and props arrive in whatever order the mutation stream carries
them.

One smaller decision that will matter later: the background colour is built in
an explicit sRGB space. Letting Core Graphics pick a device space shifts every
colour slightly on a wide-gamut display — not wrong exactly, but different from
the same app on Linux, which is the one thing this project is trying not to be.

## Checking it

`demo_layout_appkit.mm` carries the same six boxes at the same coordinates as the
GTK `demo_layout_gtk`, so the two are comparable by looking rather than by argument.
It renders offscreen to a PNG with `BASALT_SNAPSHOT=out.png`, and the picture is
the one the GTK demo produces.

Getting that snapshot working found something worth writing down, because it
cost an hour and would have cost it again. **AppKit does not attach a subview's
layer to its superview's layer when the subview is added.** It assembles the
layer tree during a display cycle, and a view with no window never has one. The
first snapshot came out as the root's background colour and nothing else — which
reads as "the children did not paint" and is actually "the children are not in
the layer tree". The fix is an offscreen `NSWindow` that is never ordered front,
plus one `[window display]`.

`BASALT_DUMP_TREE=1` prints the tree instead. Useful for diffing against the GTK
side, and worth being clear that it proves much less: it prints the frames that
were set, so it would look identical whether or not the flip works.

Seven unit tests in `basalt_appkit_tests`, sharing the harness the GTK suite uses. That
sharing needed a small change — `TestHarness.cpp` had `main` and `gtk_init` in
it, so it was not the toolkit-free thing its header claimed. The runner is now
just the runner and each suite brings its own entry point.

## What this is not

It paints. Nothing drives it.

There is no macOS mounting manager, so no `ShadowViewMutation` has ever reached
one of these views. There is no text, no images, no input, no scrolling, no
event dispatch, no host, no window, and React Native's C++ core is not linked
into the macOS build at all. Every one of those exists on the GTK side and is
the bulk of what a platform is.

So the honest summary is that this is the first of the GTK layer's files, not a
tenth of the GTK layer. The next piece is the mounting manager, and it is the
piece that will say whether `GtkMountingManager` was mostly GTK or mostly the
mutation walk — the mutation walk being the part worth sharing.
