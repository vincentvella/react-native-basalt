# Phase 39 — the Windows view layer

> **Done, 2026-09-11.** A Win32 view layer over Direct2D that paints, with a
> snapshot and the project's first rendering assertions. 17 tests, and the demo
> renders the picture the other two hosts do. No Fabric behind it yet.

Phase 18 said "Windows is the same shape and cannot be verified here", because
the machine was a Mac. The machine is now a Windows box, which changes which
half of this project can be looked at and which half has to be taken on trust.
Both of the other hosts are now the untestable ones: the Mac is gone, and WSL2
will not start on this machine because virtualisation is disabled in firmware,
so there is no Linux either. That is a fact about the desk rather than about the
code, but it is the reason this phase is a *view layer* and not a port of
anything already working.

## The decision that makes this platform different

On GTK a view is a `GtkWidget` and on macOS an `NSView`. The obvious Windows
translation is a child `HWND` per view, and it is wrong three times over.

An `HWND` is a kernel object: USER32 caps a process at ten thousand of them by
default, and a list of a few hundred rows would spend a meaningful fraction of
that budget on things that are conceptually just rectangles. An `HWND` clips
rectangularly and cannot be rotated, so `transform` and rounded
`overflow: hidden` — both of which the GTK side already has — would have to be
reimplemented anyway. And the message-routing that a window buys is exactly what
this project does not want: React Native does its own hit testing, and
`gtk_widget_pick` was only ever a convenience.

So the host owns **one** `HWND` for a surface, and `RnWin32View` is a plain C++
object. That makes this layer closer to GTK's snapshot walk than to AppKit's
layer tree, and it means the file owes the platform nothing but Direct2D. The
one place a real window is still the right answer is `<TextInput>`, where an
`EDIT` peer brings input methods, selection and every Windows key binding with
it — the same bargain GTK's `GtkText` and AppKit's `NSTextField` make, and for
the same reason.

Two smaller decisions came free. There is **no flip**: Win32 and Direct2D put
the origin at the top left, where React Native puts it, so the coordinate test
the AppKit side needs has nothing to catch here. And painting is **immediate
rather than retained**: DirectComposition would give a visual per view, which is
the closer analogue of `CALayer`, but it would make the offscreen snapshot a
second, separate path. One Direct2D walk renders identically into a window and
into a WIC bitmap, so the picture the tests compare is made by the code that
draws the app.

## The thing this platform can do that the others cannot

`plan/backlog.md` has carried this since GTK:

> No rendering assertions: the widget tree says a view has a colour and a frame,
> not that the right pixels reached the screen. This is not theoretical — GTK's
> cairo renderer mangled every transform in the demo and no test noticed.

It stayed open because closing it costs a display server on Linux and an
offscreen window plus a display cycle on macOS. Direct2D renders into a WIC
bitmap with no window, no device and no display connection, so on Windows a
rendering assertion is a function call. `Win32Snapshot` therefore has two exits
from one render — a PNG, and a pixel buffer — and `tests/test_win32_paint.cpp`
asserts on the second.

Seven of those, and they are chosen to be the ones a tree dump cannot make:

- A child lands at its frame, and nothing lands where it should not.
- Opacity composites the subtree as a unit, not per brush.
- `overflow: hidden` clips and `overflow: visible` does not.
- A quarter turn rotates about the view's centre **and in the right direction**.
  The card is deliberately wide and short so the turn changes which pixels are
  covered, and it carries a marker in one corner so that a quarter turn and a
  three-quarter turn are distinguishable — which is to say, so that a transposed
  matrix fails. That is the mistake `plan/decisions.md` describes catching on
  GTK with a picture and a human eye; here it is an assertion.
- `zIndex` reorders painting, while `test_win32_view.cpp` separately asserts it
  does not reorder the child list.
- A scroll offset moves children at paint time and leaves their frames alone.
- A corner radius rounds the background.

None of that is Windows-specific in what it checks. It is Windows-specific only
in being cheap here, which is an argument for the other two hosts eventually
borrowing the shape rather than for leaving them as they are.

## Checking it

`demo_layout_win32.cpp` carries the same six boxes at the same coordinates as
`demo_layout_gtk` and `demo_layout_appkit`, so all three are comparable by
looking rather than by argument. `BASALT_SNAPSHOT=out.png` renders it offscreen;
`BASALT_DUMP_TREE=1` prints the tree; with neither it opens a window. A console
subsystem rather than a GUI one, deliberately — the dump has to reach a pipe,
because `scripts/compare_hosts.sh` is what reads it.

`describeTree` is byte-for-byte the format the other two produce, down to the
field order, and the test that says so uses the same expected string
`tests/test_appkit_view.mm` does. That is not tidiness: `compare_hosts.sh` diffs
these dumps line by line, and a field printed in the wrong place reads as every
line differing.

The demo's own dump:

```
view tag=1 frame=(0,0 640x420) bg=#1f2129ff
  view tag=2 frame=(32,32 240x160) bg=#4d8cf2ff
    view tag=5 frame=(24,24 120x90) bg=#ffffffd9
  view tag=3 frame=(296,32 240x160) bg=#f27359ff
  view tag=4 frame=(32,224 504x140) bg=#59cc8cff
    view tag=6 frame=(24,24 160x90) bg=#1a1a1aff opacity=0.4
```

and the picture is the one Linux and macOS produce.

Seventeen tests pass, and two of them were worth being nervous about before they
did. Direct2D composes row-vector style, so `a * b` means apply `a` then `b`,
which is the opposite of the column-vector convention CSS `matrix3d` is written
in — and the two coincide for translation and scale and are transposes for
rotation, which is exactly the difference no axis-aligned test can see. The
marker in `win32_paint_rotates_about_the_centre_and_in_the_right_direction` is
what says the composition and the index mapping are both right. The GTK side
found the same class of bug with a demo and a human eye; see
`plan/decisions.md`.

It builds warning-clean under `/W4 /permissive-` two ways: with clang-cl 19.1.5
under Ninja, which is how development here goes, and with `cl` 19.44 under the
Visual Studio generator, which is what CI does. Both run all seventeen. That
split is deliberate rather than thoroughness for its own sake — CI needing
neither a `vcvars` step nor Ninja on the runner image is one less thing to
depend on, and it means the two compilers this package claims to support are
each built by somebody.

## What had to change elsewhere

One thing, and it was already latent. The root `CMakeLists.txt` claims each
platform package "skips itself where it cannot build", and the GTK one did not:
it ran `find_package(PkgConfig REQUIRED)` and `pkg_check_modules(GTK4 REQUIRED)`,
so configuring the repository anywhere without GTK failed the whole configure
rather than the one package. True on Windows, and it would have been true on a
Mac without brew's gtk4 too. Those two are now soft, with an early return and a
status line; everything after them stays `REQUIRED`, because a box with gtk4 and
no pangoft2 is a broken installation rather than an absent one.

## What this is not

It paints. Nothing drives it.

There is no Windows mounting manager, so no `ShadowViewMutation` has ever
reached one of these views. There is no text, no images, no input, no scrolling,
no event dispatch, no host, and React Native's C++ core is not built on Windows
at all. Every one of those exists on the GTK side and is the bulk of what a
platform is.

So the honest summary is the same one phase 18 gave: this is the first of the
GTK layer's files, not a tenth of the GTK layer. The next piece is the mounting
manager, and phase 19 already answered the question it would have asked — the
mutation walk is portable and lives in `core/MountingWalk.h`, so what this
platform owes is seven operations and a props translation.

The larger unknown is behind that, and it is not the view layer at all: nothing
in `packages/react-native-basalt` has ever been compiled with MSVC. folly,
glog, boost and React Native's own ReactCommon all build on Windows in
react-native-windows, so it is known-possible rather than speculative, but
"known-possible" is what phase 17 said about the second view layer too.

## Where the toolchain sits

The machine had no C++ compiler, no Windows SDK, no CMake and no Ninja. What it
has now is Visual Studio 2022 Build Tools with the C++ workload and the Clang
component, Ninja from winget, and CMake as the portable ZIP under
`C:\Users\vince\Tools` — winget's MSI wanted an elevated install server that
never came up, and the ZIP needs no administrator at all, which is a better
answer for a tool that is only ever run from a shell.

clang-cl where there is a choice, which is `plan/decisions.md`'s "clang, not
GCC" carried across. CMake reports clang-cl as MSVC, which is why the warning
flags in the package's CMakeLists are chosen on `if(MSVC)` rather than on the
compiler id — and is worth knowing before someone adds a `-Wsomething` there and
watches it not apply.

One thing this does not have and should: WSL2 will not start, because
virtualisation is disabled in the machine's firmware. So the Linux host cannot
be built or run here, and `scripts/compare_hosts.sh` — the thing that would say
whether this platform's dump really matches GTK's rather than matching what a
test file claims GTK's is — has not been run against Windows at all. The
byte-for-byte claim rests on reading `rn_view_describe_into` and copying it,
which is weaker than it sounds.
