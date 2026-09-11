# Phase 5 — Input

> **Done, 2026-09-09.** `src/GtkTouchDispatcher.*`, `src/GtkRunLoopObserver.*`,
> and the event-emitter registry in `GtkMountingManager`.

**Goal:** an app where pressing something does something. Success is `onPress`
firing on a `<Pressable>` and the resulting `setState` reaching the screen.

## What it took

1. **Keep the event emitters.** `ShadowView` carries an `EventEmitter::Shared`.
   The mounting manager now stores the `TouchEventEmitter` per tag alongside the
   widget, refreshed on Update because a clone carries a new instance, and
   dropped on Delete.
2. **Hit test.** `gtk_widget_pick` on the surface root, then walk up to the
   nearest `RnView`. Every view is already allocated at its Yoga frame, so GTK's
   own picking gives the answer React Native's hit testing wants.
3. **Translate.** `GtkGestureClick` and `GtkEventControllerMotion` on the root
   become touchstart/touchmove/touchend/touchcancel.
4. **Drive the beat.** The step that was missing entirely; see
   `plan/decisions.md`.

## Deliberately not done

Hover, keyboard, focus, multi-touch, and `setIsJSResponder`. See
`plan/backlog.md` for why each is separable.

## The gap in the evidence

Synthesising a real pointer event needs accessibility permission the automated
runs on this machine do not have, so `BASALT_TEST_TAP` injects at the point
GTK's gesture callback would call. Hit testing, emitter lookup, the event beat,
the responder system and the re-render are all exercised. GDK's routing of a
real click into the controller is not.
