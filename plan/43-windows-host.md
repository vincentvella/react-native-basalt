# Phase 43 — the Windows host

> **Done, 2026-09-12.** JavaScript renders on Windows. Hermes evaluates a
> bundle, Fabric diffs a shadow tree, `Win32MountingManager` mounts the
> mutations, and Direct2D paints them in an HWND.

Everything before this was driven by hand: `demo_layout_win32` places boxes,
`mount_harness_win32` feeds hand-built `ShadowViewMutation`s. This is the first
thing on this platform that nobody wrote the mutations for.

    started surface 1 (no module; raw Fabric script)
    --- committing tree 1 from JS ---
    --- committing tree 2 from JS ---

    view tag=1   frame=(0,0 900x700)
      view tag=101 frame=(24,24 836x376) bg=#9b59f6ff
      view tag=100 frame=(48,48 160x90)  bg=#e6eeffff
      view tag=104 frame=(24,416 852x260) bg=#56c98aff

That is `js/demo.js` -- a surface driven straight through
`nativeFabricUIManager`, no React and no react-native JavaScript -- which is the
same first light-up both other hosts had.

## The event beat, which Windows cannot install

`EventQueue::onEnqueue` only sets a flag. Nothing an `EventEmitter` produces
reaches JavaScript until something calls `RunLoopObserverManager::onRender()`,
and React Native asks for that at `Activity::BeforeWaiting` -- once the loop has
drained its work and is about to sleep.

The other two platforms *install* something. iOS and macOS add a
`CFRunLoopObserver` on `kCFRunLoopBeforeWaiting`; GTK builds the equivalent out
of a `GSource` whose `prepare()` does the work and never reports itself ready.
Win32 has no such hook. A message loop is `GetMessage`/`DispatchMessage` and
there is no callback for "about to block".

So the loop itself is the observer. `Win32RunLoopObserver` drains with
`PeekMessage` until the queue is empty, beats, and only then blocks in
`MsgWaitForMultipleObjectsEx`. That is exactly what "having drained whatever
else was pending" means, and blocking rather than polling is what keeps an idle
app at zero CPU -- the same objection `plan/decisions.md` raises against
`gtk_widget_add_tick_callback`.

One flag in there is worth more than it looks. `MWMO_INPUTAVAILABLE` makes a
message that was *already* queued when the wait begins count as an event.
Without it the loop can sleep with work pending until the next message arrives,
and since the `PeekMessage` above has just emptied the queue that is nearly
always fine. "Nearly always" is how an application hangs once an hour.

## Repainting, which nothing does on its own

GTK's mounting manager calls `gtk_widget_queue_draw` and AppKit's sets
`setNeedsDisplay:`, both from inside the manager, because on those platforms a
view *is* a widget and knows how to ask. A Windows view is a plain C++ object
and only the host has an HWND, so the manager grew `setOnDidMount` and the host
hands it an `InvalidateRect`.

Without it every mutation arrives correctly and nothing moves on screen until
the window happens to be invalidated by something else -- a resize, another
window passing over it. That reads as "mounting is broken" and is not, which is
why it is worth naming.

## The choreographer, which is a placeholder and says so

`Win32AnimationChoreographer` is a sixteen-millisecond timer. macOS uses a
CADisplayLink and GTK the widget's frame clock, both of which are told by the
compositor when a frame is due; Win32 has no equivalent for an ordinary window.
Doing it properly means `DwmGetCompositionTimingInfo` and `DwmFlush` on a
thread of its own, because `DwmFlush` blocks and a blocked UI thread is worse
than a slightly wrong frame interval.

`plan/backlog.md` already records that worklets and Reanimated run on a
sixteen-millisecond timer on *both* other desktops, for a related reason. This
is a third instance of the same gap rather than a new one. What it does get
right is pausing when nothing is animating, which matters more on a laptop than
the interval does.

## What the host does not have

**Input.** There is no `Win32TouchDispatcher`, so `WM_LBUTTONDOWN` reaches the
window and stops. Hit testing is already written and has twelve tests --
`hitTest` in `RnWin32View.h`, including the transform inversion neither other
platform does -- so what is left is turning a mouse message into a React Native
touch and handing it to the event emitter the mounting manager already keeps per
tag. Until that exists nothing on screen is pressable, which is also why the
host defaults to the raw-Fabric script rather than to a React app.

That is phase 44, which is the next thing done, and `<ScrollView>` is phase 45.
`<TextInput>` is still unclaimed, so a screen with a form in it renders with a
hole where the field should be.

## Two escape hatches, matching the other hosts

`BASALT_DUMP_TREE` writes the view tree on the way out, so an automated run can
assert on what React produced rather than on a screenshot. `BASALT_SNAPSHOT`
renders the surface to a PNG, which the other two hosts do not have on the host
itself and which is worth more here: the tree says which mutations arrived and
says nothing about whether any of them painted, and on Windows those really are
separate questions.
