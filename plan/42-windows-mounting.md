# Phase 42 — the Windows mounting manager, and the third data point

> **Done, 2026-09-12.** Real `ShadowViewMutation`s mount onto real views. 110
> tests, and the mount harness produces the pictures the other two produce.

Phase 19 divided a mounting manager into a portable mutation walk and seven
operations a platform supplies, and tested the division by writing a second
platform. Two platforms agreeing is weak evidence when both are toolkits with a
widget object per view: GTK has `GtkWidget`, AppKit has `NSView`, and a division
that suits both might suit nothing else.

This is the third, and it is not one of those. A Windows view is a plain C++
object with no toolkit behind it -- `plan/decisions.md` records why -- and the
mounting manager still comes to seven operations and a props translation.
`core/MountingWalk.h` was not modified.

## Where Windows is actually different

One operation, and it is `destroyView`.

On GTK the registry holds a `g_object_ref_sink` and this is the matching
`g_object_unref`. Under ARC it is *nothing at all*: erasing the registry entry
drops the last strong reference, and phase 19 kept the hook anyway because the
walk still has to say when a view stops being owned even where saying it costs
nothing. A plain C++ object has neither mechanism, so here the ownership is
written out and `delete` is the whole body.

That makes Windows the platform where Fabric's "a Remove detaches but must not
destroy" rule is enforced by nothing but the code being right, which is why
`win32_delete_removes_the_view_from_the_registry` and
`win32_remove_detaches_but_does_not_destroy` are worth having as a pair.

## Four seams, because nothing links without them

Phase 17 said a new desktop owes the core one seam and phase 19 corrected it to
three. The real number, counted by what a mounting manager cannot link without,
is four -- and the fourth was hiding in plain sight.

- **`ComponentRegistryWin32`**, which claims `View`, `Paragraph` and `Image`,
  and deliberately not `ScrollView` or `TextInput`. Leaving `ScrollView` out
  does not fail loudly: the registry substitutes `UnimplementedNativeView`,
  which has no `ScrollViewState`, and the symptom is a `ScrollView` that renders
  and never scrolls. Worth knowing before wondering why.
- **`DirectWriteLayoutManager`**, replacing React Native's stub, because
  `ParagraphComponentDescriptor` constructs a `TextLayoutManager` and a platform
  without one fails to link on a constructor rather than on anything to do with
  text.
- **`FontRegistryDirectWrite`** and **`ColorSchemeWin32`**, the two the probe
  already knew about.
- **`PlatformServicesWin32`**, which is the one worth naming. It is the
  clipboard and `ShellExecute` and a `MessageBox`, and it is also
  `postToUiThread` -- the marshalling the mounting manager depends on. On
  Windows that is a message-only window and `PostMessage`, which is the exact
  analogue of `g_idle_add_full` and `dispatch_async_f`, making the same FIFO
  promise. Without the ordering that promise buys, a command that arrived before
  the transaction creating the view it names would find nothing and be dropped.

## What `<Text>` turned out to need

The view layer's text tests all used one style for a paragraph, and that was
enough to hide something: React Native's `<Text>` is not one string with one
style. `ParagraphShadowNode` folds an entire subtree into an `AttributedString`
of *fragments*, each with its own font, size and weight, and a layout built from
the first fragment's style renders `Hello <b>world</b>` entirely unbold -- and
measures it wrong, which is worse, because Yoga then allots the wrong box.

So `RnWin32TextLayout` grew `createFromRuns`, applying each run's family, size,
weight and slant over its own character range. Ranges are counted in UTF-16 code
units rather than bytes or code points, so each run's extent is measured after
conversion; getting that wrong shifts every style after the first emoji. The
colour is still per-paragraph, because DirectWrite carries it as a drawing
effect rather than a range attribute.

## The harness, and the picture

`mount_harness_win32` runs the same two transactions
`gtk/mount_harness_gtk.cpp` and `appkit/mount_harness_appkit.mm` run, against
the same boxes. Four creates and four inserts, one of them nested; then an
update that recolours and shrinks, and a remove-then-delete in the order Fabric
guarantees. `BASALT_SNAPSHOT_DIR` renders both to PNGs; without it, a window,
with the second transaction two seconds in.

Both pictures are what Linux and macOS produce. The second one has a detail
worth pointing at: the nested white box now hangs out of the bottom of its
shortened parent, because `overflow: visible` is React Native's default and the
parent went from 160 tall to 100. A clip applied where none was asked for would
be invisible in the tree dump and obvious here, which is the argument for the
harness existing.

One difference from the AppKit harness, and it is an absence rather than a
change. That one spins its run loop after each `executeMount`, because
`dispatch_async_f` has genuinely queued the work. Here `postToUiThread` runs
inline when no host has installed a message loop, so there is nothing to drain
-- stated in the source, because it otherwise reads as a missing step.

## The image load, which was wrong and is now not

It was synchronous at first, which stalls the UI thread on a remote image.
Win32ImageLoader fixes that, and doing it turned up three things worth keeping.

**The completion captures a tag, not a view.** A view can be deleted while its
bytes are in flight, so the completion looks the tag up again -- and then checks
that the URI it loaded is still the one the view wants, because the source can
change twice while the first load is running. GTK reached the same arrangement.

**A load can outlive the loader.** The worker and the queued completion both
outlive any call, and a mounting manager can be destroyed between them on
surface teardown rather than only at exit. So the cache lives in a shared state
object the in-flight work holds a reference to. The AppKit loader captures its
`this` raw and has the bug this avoids.

**Going asynchronous unconditionally would have been worse than staying
synchronous.** `postToUiThread` runs its work inline when no host has installed
a message loop, which is right for the harness and the tests -- and "inline" on
a worker thread is not the UI thread, so the completion would have reached into
the mounting manager's registry from the wrong one. So the loader asks
`hasUiThread()` and does not spawn a worker when there is nothing to marshal
back to. The lock on the shared state is the belt to that pair of braces.

And it found a real bug in the shared half. `core/ImageBytes.cpp` strips
`file://` and keeps the rest, so `file:///C:/x` became the path `/C:/x` --
correct on POSIX, where the URI path and the filesystem path are the same
string, and unopenable on Windows, which has no root above a drive letter. Fixed
under `#ifdef _WIN32` rather than portably: the other two desktops cannot be
tested from here, and a shared behaviour change is not worth making blind for a
case that cannot arise there.

## What is not here

`<ScrollView>`, `<TextInput>`, the touch dispatcher and the host. And nothing
evicts the image cache -- `clearCache` exists and nobody calls it -- which is
the same unbounded growth `plan/backlog.md` records for GTK.
