# Phase 40 — the Windows suite catches the other two

> **Done, 2026-09-12.** 62 tests, against 60 on GTK and 60 on AppKit. Hit
> testing, DirectWrite text, WIC images and UI Automation.

Phase 39 left a view layer that painted and seventeen tests. This is the rest of
what a Windows view layer can be asked without React Native's C++ core, which at
the time had never been compiled with MSVC.

Read the 62 carefully. It is not parity with what the other two hosts *do* -- it
is parity with what they are *asked*, minus the eight tests on each side that
need `basalt_core`: mounting, `<TextInput>`, input dispatch and platform
services. Those are phase 41's problem, not this one's.

## Hit testing, and a bug it found on macOS

Both other hosts inherit picking from their toolkit -- `gtk_widget_pick` and
`-[NSView hitTest:]` -- so the work there is making the toolkit's geometry agree
with Fabric's. There is no toolkit geometry here, so it is written out.

That is more code and one fewer thing that can disagree, and `localToParent` is
what makes the difference: the six numbers of a view's affine transform,
composed in exactly one place, built into a `Matrix3x2F` by `paint` and inverted
by `hitTest`. A rotated view is therefore clickable where it is drawn, and
cannot stop being so without both halves changing together.

Writing that found that **AppKit does not do this**. `RnAppKitHitTest` walks
`child.frame` and never consults the layer transform, so on macOS a rotated view
is clickable where it used to be -- the bug GTK avoided by composing the
transform into allocation, which is the reason `plan/decisions.md` gives for
putting it there rather than in the paint. Not fixed here: it is not this
platform's file and there is no Mac on this desk to check a fix on. The backlog
carries it.

## Text

DirectWrite, in Core Text's shape rather than Pango's: one paragraph object
built once and used by both measurement and painting, so Yoga cannot be told one
size and the view paint another.

Two of its tests are other platforms' regressions repeated here, because the bug
each describes is one any text engine can have. A font size interpreted as
points and resolved against 96dpi renders a third too large -- free to avoid
here, since a DIP at 96dpi *is* a density-independent pixel. And a default
ellipsize mode that collapses a wrapping paragraph to one line, which is Pango's
trap; DirectWrite will not trim without a height, so declining to set one until
`numberOfLines` says to is what makes the same default safe.

One thing DirectWrite gives that Pango does not: a shared factory is documented
thread-safe, and this builds a fresh `IDWriteTextLayout` per call rather than
mutating a held one. GTK serialises every measurement on a single mutex because
Pango's font map is not documented reentrant, and hides the cost behind a cache.
Fabric's layout thread and the UI thread can measure at the same time here.

## Images

WIC, held device-independently. An `ID2D1Bitmap` belongs to the render target
that made it, so holding one would tie an image to a window and make it useless
to the offscreen snapshot; the device bitmap is a cache keyed on the target. The
fit arithmetic is `rn_view_snapshot`'s, in the same order, so the three desktops
crop and centre identically.

These tests ask what was *painted* rather than what rect was computed, and the
difference is not academic: `cover` that scales correctly and forgets to clip is
indistinguishable from correct in a rect, and there is a test for the clip
alone.

One of them was wrong on its first run, which is the best argument for them. It
sampled the exact seam between the image's two colour halves and read the
interpolation across it -- `(115,0,140)` where it wanted red. A real answer to
the wrong question, and a mistake a destination-rect comparison cannot make
because it cannot make any.

## Accessibility, where Windows is genuinely different

GTK and AppKit both take accessibility as properties pushed onto a view:
`gtk_accessible_update_property`, `setAccessibilityRole:`. UI Automation inverts
it -- a provider object is asked questions by property id and answers them. So
where those two mounting managers push, this one has to be ready to be pulled,
and the tests ask the provider the way a screen reader would rather than reading
back a property they just set.

Two things fall out of the inversion, both in the good direction.

A `GtkAccessible`'s role is construct-only, which is why `plan/decisions.md`
records that `accessibilityRole` cannot change after mount on Linux. Nothing is
baked into a widget class here, so it can, and `accessibility_role_can_change_after_mount`
says so. The mounting manager should not copy GTK's restriction when it arrives.

And the tri-state survives. Leaving `checked` unset is not the same as setting
it false, so the provider answers `VT_EMPTY` rather than `ToggleState_Off`: a
view that never mentions being checked is not an unchecked checkbox, and
Narrator would read it out as one. `IsEnabled` is the deliberate exception,
because UIA has no unknown for it and its default is React Native's.

Three role mappings are approximations UIA forces, named in the source rather
than left to be rediscovered. It has no switch, so a switch is a checkbox; no
heading control type, since headings are a property on a text element, so
"header" is text; and "alert" is an event rather than a control type, so it
rests as a group until there is a live region to raise it from. The vocabulary
is otherwise exactly `GtkMountingManager.cpp`'s, because a role one host honours
and another silently ignores is a difference no tree-diff would catch -- all
three print React Native's own string.

The control *patterns* -- Toggle, SelectionItem, ExpandCollapse, Invoke -- are
not here. A pattern that reports a state and cannot be driven tells a screen
reader the control can be operated and then does nothing, which is worse than
not offering it. The states are answered as plain properties, which is read-only
and honest, and the patterns land with the host alongside
`IRawElementProviderFragment`, whose fragment root is an HWND.

## What is still missing

Everything behind `basalt_core`, which is phase 41. In test terms that is the
mounting walk, `<TextInput>`'s controlled-value loop, the touch dispatcher and
platform services -- and in real terms it is that nothing drives any of this
yet. Sixty-two tests against a view layer no mutation has ever reached is a
well-tested component, not a platform.
