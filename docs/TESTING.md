# Testing

Two suites. Both need a display, because everything here is a GTK program.

```bash
./build/rn_tests                    # unit
scripts/bundle.sh ../react-native --prod
scripts/integration_test.py         # end to end
```

On a headless machine, run either under a virtual display:

```bash
xvfb-run -a ./build/rn_tests
xvfb-run -a scripts/integration_test.py
```

`rn_tests` exits 77 rather than failing when it cannot reach a display, which
is the convention for "skipped".

## `build/rn_tests` — the unit suite

Everything reachable without a JavaScript runtime. No React, no Metro, no
Hermes: mutations are hand-built, the way `mount_harness` builds them.

| File | What it pins down |
|---|---|
| `tests/test_mounting.cpp` | The mutation walk. Create/Insert/Remove/Delete/Update ordering, insert indices, nesting, that Remove detaches without destroying, that Delete unregisters, and that a stray Insert for a deleted tag is ignored rather than fatal. |
| `tests/test_view.cpp` | The widget layer. Frames, child order, that a scroll offset moves children, that `measure` reports zero so GTK never competes with Yoga, and that dispose unparents children. |
| `tests/test_text.cpp` | Pango measurement. Wrapping, `numberOfLines`, that a bigger font measures bigger, and two regression tests: that font sizes are absolute rather than points, and that the default ellipsize mode does not collapse a wrapping paragraph to one line. |
| `tests/test_hittest.cpp` | Hit testing. Depth, sibling order, misses, and that it follows a scroll offset. |
| `tests/test_image.cpp` | The image loader. Decoding, the cache answering synchronously, and the three ways a load can fail. |

The harness is `tests/TestHarness.h`, about sixty lines, no dependencies. Its
assertions are `EXPECT`, `EXPECT_EQ` and `EXPECT_NEAR` rather than `CHECK*`
because glog — which React Native pulls in almost everywhere — already defines
`CHECK` and `CHECK_EQ`. Those win the preprocessor silently, and a failing
assertion then aborts the process instead of being reported, taking every later
test with it. That happened; hence the naming.

Hit tests put their widgets in a real window. `gtk_widget_pick` skips widgets
that are not mapped, and a widget is only mapped inside a shown window.
Allocation then happens in the frame clock's layout phase, so the tests build
the tree first, show the window, and spin the main loop until a frame has
actually gone through. Draining the context without waiting is not enough: with
no frame due it returns immediately and the children are still unallocated.

## `scripts/integration_test.py` — the end-to-end suite

Runs the real host against the real Metro bundle, injects taps, and asserts on
the widget tree the host writes on the way out.

This is what `RN_LINUX_DUMP_TREE` exists for. The path this project exists to
provide — JavaScript, React, Fabric, the mounting manager, GTK — can only be
exercised by running the whole thing, and before the dump the only way to check
the result was to look at a screenshot.

Current scenarios:

1. **Initial render.** React produced text, the image loaded and decoded, all
   24 rows exist, the ScrollView clips, and it starts at the top.
2. **`scrollToEnd`.** The offset moved, and `onScroll` reached React, whose
   rendered label agrees with the widget tree. The tap deliberately lands on the
   button's *label*, so passing also means a touch on a child bubbled to the
   `Pressable` that handles it.
3. **Scroll away and back.** `scrollTo({y: 0})` returns to the top and the label
   follows.

The scenarios read coordinates from the demo's layout in `js/index.js`. Change
the demo's spacing and the coordinates need changing too — the alternative,
searching the dumped tree for a button by its label and tapping its centre,
would be more robust and is worth doing if this list grows.

## What is still not covered

**GDK's own event delivery.** Taps enter at the point GTK's gesture callback
would call them, and wheel scrolling is exercised only through the `scrollTo`
command path. Everything downstream is covered; what is not is GDK routing a
real pointer or scroll event into the controller, and the wheel-notch to pixels
conversion.

Synthesising a real input event means driving the window system. On macOS that
needs accessibility permission, which an automated run does not have — hence the
injection point. Two ways to close it, both on the platform this actually
targets:

- **X11:** `xdotool mousemove 657 650 click 1`
- **Wayland:** `ydotool`, or `wtype` for keys

Either would let the integration suite drive the host the way a person does. It
is worth doing on the Arch box; see `docs/HANDOFF.md`.

**Rendering.** Nothing asserts on pixels. The widget tree says a view has a
background colour and a frame, not that the right pixels reached the screen. A
screenshot comparison would catch paint bugs the tree cannot — wrong z-order,
a missing clip, text drawn in the wrong colour — but it needs a reference image
per machine, and font rendering differs enough between them that the references
would not travel. Reasonable to add on one fixed machine, not as a portable
suite.

**Threading.** The mounting manager asserts it is on the main thread, and text
measurement takes a mutex, but nothing exercises the JS thread and the main
thread concurrently.
