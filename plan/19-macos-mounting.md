# Phase 19 — the macOS mounting manager, and what it cost

> **Done, 2026-09-10.** Real `ShadowViewMutation`s mount onto real `NSView`s.
> `<View>` only, and the mutation walk is now shared with GTK.

Phase 18 ended with a question: was `GtkMountingManager` mostly GTK, or mostly
the mutation walk? The walk being the part worth sharing — if it is portable, a
third desktop is a view layer; if it is not, every platform reimplements Fabric's
semantics and gets them subtly differently.

It is portable. The proof is that it moved into `core/MountingWalk.h` and the
GTK suite still passes 60/60 without a single test changing.

## What the walk turned out to be

Fabric's mutation stream has rules that the interface does not state, and most
of them are the kind you get wrong once:

- A `Create` allocates but does not attach. The `Insert` may be in a later
  transaction entirely.
- A `Remove` detaches but must not destroy. A reparent is a Remove and an Insert
  with no Delete between them, so a view has to survive the gap — and the
  registry's strong reference is the only thing holding it.
- An `Insert`'s index counts positions in the parent's *final* child list, so a
  later mutation lands between two existing children. An append-only
  implementation passes every other test.
- Virtual views exist in the shadow tree only, to keep an `EventEmitter` alive.
  They have no peer to parent.
- An `Update` carries a fresh emitter instance. Keeping the old one delivers
  touches to a stale target.
- A mutation naming a tag that no longer exists has to be survivable. That is
  what a bug upstream, or a transaction racing a surface teardown, looks like
  from down here.

None of that is about a toolkit. It is now written once, and each platform
supplies seven operations that do touch a view: `createView`, `createRootView`,
`destroyView`, `insertChild`, `removeChild`, `updateView`, `forgetTag`.

CRTP rather than virtual dispatch, because the view type differs — `RnView *`
under GObject, `RnMacView *` under ARC — and a template keeps it concrete in each
translation unit with no type erasure on a path that runs for every mutation of
every frame.

The ownership difference is the one that could have bitten. On GTK the registry
holds a `g_object_ref_sink`; under ARC it holds a strong pointer and erasing the
entry is what releases it. `destroyView` is a no-op on macOS, and the hook stays
anyway, because the walk still has to say *when* a view stops being owned even on
a platform where saying it costs nothing. `mac_remove_detaches_but_does_not_destroy`
is the test that would catch it if that stopped being true.

## Correcting phase 17

Phase 17 asked what a new desktop platform owes the shared core, answered "one
seam — fonts", and made the argument with a linker, which is why it seemed
trustworthy. Linking an actual second platform found two more.

**`TextLayoutManager` is a seam.** This build deliberately drops React Native's
stub `TextLayoutManager` so the Pango one can be the only definition. That is
invisible until a platform without a text engine tries to link, at which point it
fails on a constructor rather than on anything to do with text.

**The component registry is a seam, and it is what made the first one visible.**
`getDefaultComponentRegistryFactory` was a shared inline function registering
View, Paragraph, Text, RawText, Image, ScrollView and TextInput, on the
reasonable-looking grounds that a descriptor is portable C++ and registering one
costs nothing. It does not: `ParagraphComponentDescriptor` constructs a
`TextLayoutManager`. So the registry moved beside each platform —
`gtk/ComponentRegistryGtk.cpp` with all seven, `mac/ComponentRegistryMac.mm` with
one.

That is a better arrangement than the shared version was, and not only because it
links. The registry and `hasComponent` are two statements of the same fact, and
before this they could disagree silently. When they do, the registry wins and
Fabric builds shadow nodes nothing can mount — which renders as blank rectangles
rather than as an error. Keeping them in the same directory does not enforce
agreement, but it does put them where the same person will read both.

So the count is three, not one. The probe in `core/portability_probe.cpp` caught
neither of the new ones, because it links the core alone and nothing in the core
references either. That is a real limit of the probe, not an oversight to fix by
adding two symbols to it: a seam is only discoverable this way once something
uses it.

## Where it gets to

`mount_harness_mac` runs the same two transactions the GTK harness does, against
the same boxes, with no JavaScript anywhere. Four creates and four inserts, one
of them nested; then an update that recolours and shrinks, and a remove-then-delete
in the order Fabric guarantees. `RN_MAC_SNAPSHOT_DIR` renders both to PNGs. Both
pictures are what Linux produces.

Sixteen macOS unit tests, nine of them the mounting ones, which are deliberately
the same nine questions `tests/test_mounting.cpp` asks of GTK in the same order
with the same tags. Most of what they now test is whether the sharing holds. The
day it stops, they fail here rather than in an app.

## What is missing

`hasComponent` returns `View` and `RootView`, and that is the honest summary of
this platform. Everything else is a named piece of work:

| Missing | What it needs |
|---|---|
| `<Text>` | A Core Text `TextLayoutManager`, and the fonts seam behind it |
| `<Image>` | An image loader; React Native's cxx `ImageManager` produces no pixels |
| `<ScrollView>` | A clipping scroller, `onScroll`, and scroll state |
| `<TextInput>` | An `NSTextField` peer and the controlled-value loop |
| Touch and keyboard | No events reach JavaScript at all |
| The host | No `ReactHost`, no Hermes, no window, no surface |

And within `<View>`: per-corner radii, borders, transform, z-index and
pointer-events are all unmapped. None is hard. Each needs a test that compares
the result against what Linux produces rather than against what looks plausible
on a Mac, which is the whole point of doing it this way.
