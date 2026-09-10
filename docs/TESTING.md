# Testing

Two suites. Both need a display, because everything here is a GTK program. Both
run in CI on Linux, which is where they matter: see `.github/workflows/ci.yml`.

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
| `tests/test_textinput.cpp` | The controlled-value loop. That applying a prop is not reported back as typing, that the caret survives a prop arriving mid-word, that a command carrying a stale `eventCount` is dropped, and that the `GtkText` peer is allocated inside the content inset. Props are built through React Native's own `RawProps` parser, because the fields that matter are const and only reachable that way. |

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
4. **Type into a `<TextInput>`.** The field is focused through a button rather
   than by tapping it, so this covers the `focus` command too. Passing means the
   characters reached React: the value is controlled, so what ends up in the
   widget is only there because `onChange` went up and the new value came back
   down. The demo echoes it into a sibling `<Text>`, which is what the scenario
   actually asserts on.

The scenarios read coordinates from the demo's layout in `js/index.js`. Change
the demo's spacing and the coordinates need changing too — the alternative,
searching the dumped tree for a button by its label and tapping its centre,
would be more robust and is worth doing if this list grows.

## Input: real events, and where they are not

The end-to-end suite delivers taps one of two ways, and says which at the top of
its output.

**`real`** — `xdotool` moves the pointer and clicks, so the event goes through
the X server and GDK exactly as a person's would. This is the only mode that
exercises event delivery itself. Chosen automatically when `DISPLAY` is set and
`xdotool` is installed.

**`injected`** — `RN_LINUX_TEST_TAP` calls the gesture callback directly,
skipping GDK. The fallback where a real event cannot be synthesised, which
notably includes macOS: doing it there needs accessibility permission an
automated run does not have.

Force either with `--input real` or `--input injected`.

Typing splits the same way. In `real` mode `xdotool type` sends key events
through the X server, so GDK and the input method see them. In `injected` mode
`RN_LINUX_TEST_TYPE` inserts through `GtkEditable` on whatever field has focus,
which skips the key controller and the input method and exercises everything
above them.

## Running on Linux

The project targets Linux and is developed on a Mac, so everything here should
be checked on Linux before it is believed. A VM is enough:

```bash
brew install lima
limactl start --name=rnlinux <a template with vz, 8 cpus, 16GiB, 120GiB>
limactl shell rnlinux
```

Apple Virtualization rather than QEMU: the guest is aarch64 like the host, so it
runs at native speed. Building React Native's core and Hermes under emulation
would take hours.

Inside, on Ubuntu 24.04:

```bash
sudo apt install build-essential clang cmake ninja-build pkg-config git curl \
  libgtk-4-dev libpango1.0-dev libglib2.0-dev libgoogle-glog-dev \
  libboost-dev libboost-regex-dev libfmt-dev libdouble-conversion-dev \
  libgflags-dev libssl-dev libcurl4-openssl-dev
# Node 24: React Native needs ^22.13 || ^24.3 || >= 26, and 24.04 ships 18.
curl -fsSL https://deb.nodesource.com/setup_24.x | sudo -E bash - && sudo apt install nodejs
```

Then the usual bootstrap, cmake, build.

### Rendering, on Wayland

A real compositor, headless, with no GPU:

```bash
sudo apt install sway grim mesa-vulkan-drivers libgl1-mesa-dri
printf 'output HEADLESS-1 resolution 1400x1000\ndefault_border none\n' > /tmp/sway.conf
XDG_RUNTIME_DIR=/run/user/$(id -u) WLR_BACKENDS=headless WLR_RENDERER=pixman \
  sway -c /tmp/sway.conf &
WAYLAND_DISPLAY=wayland-1 GSK_RENDERER=gl ./build/rn_linux_host build/main.jsbundle.js
grim shot.png
```

Two renderer choices, for two different layers, and they are not the same one.

`WLR_RENDERER=pixman` is the *compositor's*. Without a GPU, wlroots' default
fails with `drmGetDevices2 failed` and sway starts but draws nothing.

`GSK_RENDERER=gl` is *GTK's*, and it must not be `cairo`. GTK 4.14's cairo
renderer draws a transformed widget subtree unrotated and in the wrong colour --
the demo's rotated card comes out as a flat pink square. The GL renderer gets it
right, and works headless through Mesa's software rasteriser, so there is no
reason to reach for cairo. This cost an hour of suspecting the transform code,
which is fine on macOS and fine here under GL.

### Input, on X11

Real pointer events need a seat with input devices, which a headless Wayland
compositor does not have — sway sees no inputs, and `swaymsg seat … cursor`
silently does nothing. Getting one means logind, a session and `uinput`, which
is a lot of machinery for one assertion. X11 gives the same guarantee for far
less:

```bash
sudo apt install xvfb xdotool
Xvfb :99 -screen 0 1400x1000x24 &
DISPLAY=:99 GDK_BACKEND=x11 GSK_RENDERER=cairo scripts/integration_test.py
```

That is a genuine event through the X server and GDK. The wheel path checks out
too: five `xdotool click 5` notches move the content exactly 265 points, which
is the 53-point step in `GtkScrollViewManager` times five.

## What is still not covered

**Wayland input.** Rendering is verified on Wayland and input on X11, but not
both at once. Closing it needs a compositor with a real seat — a desktop session
in the VM, or a physical Linux machine.

**Rendering.** Nothing asserts on pixels, which is how GTK's cairo renderer
mangled every transform in the demo without a single test noticing. The widget tree says a view has a
background colour and a frame, not that the right pixels reached the screen. A
screenshot comparison would catch paint bugs the tree cannot — wrong z-order,
a missing clip, text drawn in the wrong colour — but it needs a reference image
per machine, and font rendering differs enough between them that the references
would not travel. Reasonable to add on one fixed machine, not as a portable
suite.

**Threading.** The mounting manager asserts it is on the main thread, and text
measurement takes a mutex, but nothing exercises the JS thread and the main
thread concurrently.

**macOS.** CI is Linux only, because that is the target and because every
platform-specific bug so far has been a Linux one. Development happens on a Mac,
so macOS is covered by whoever is working; it is not covered by a machine.

## CI

`.github/workflows/ci.yml`. Installs the dependencies, builds, and runs both
suites under Xvfb with `xdotool` present, so the end-to-end suite is in `real`
input mode.

It builds against a **pinned** React Native commit, in `scripts/react-native.pin`.
This project tracks `main`, which is right for development and wrong for CI: a
build that follows `main` turns every upstream change into a red tick on an
unrelated pull request. A separate `drift` job builds against `main` weekly and
is allowed to fail, so upstream churn is noticed without blocking anything.

Three caches carry the cost: React Native's `node_modules` and `third_party/`
(which holds the Hermes build) are keyed on the pin, and `ccache` on the commit
with a rolling fallback.

They are the difference between a **42 minute** run and a **2 minute** one.
Cold, a run installs React Native's dependencies (949MB of `node_modules`),
builds Hermes, and compiles 400-odd objects of React Native core. Warm, it
restores 304MB of `third_party`, hits ccache, and goes straight to the tests.

Two things about those caches that are easy to lose an hour to:

- **The `third_party` key hashes the whole of `bootstrap.sh`**, so editing a
  comment in it costs a Hermes rebuild. That is deliberate: over-invalidating is
  the safe direction, and the script changes rarely now.
- **A cache saved on a branch is invisible to `main`.** GitHub scopes caches to
  the branch that created them, and shares them only downwards, to that branch's
  descendants. So the first run after merging a branch is cold all over again,
  and only then do the caches land somewhere every later run can reach.
- **`actions/cache` will not save a path that escapes the workspace.** It says
  nothing about it either; the cache simply never appears. React Native is
  therefore cloned to `react-native-src/` *inside* the checkout rather than
  beside it. Getting this wrong is quiet until the day `third_party` hits: then
  bootstrap skips `yarn install` because codegen is already there, and bundling
  fails for want of `node_modules`.

**Rendering.** Nothing asserts on pixels, which is how GTK's cairo renderer
mangled every transform in the demo without a single test noticing. The widget tree says a view has a
background colour and a frame, not that the right pixels reached the screen. A
screenshot comparison would catch paint bugs the tree cannot — wrong z-order,
a missing clip, text drawn in the wrong colour — but it needs a reference image
per machine, and font rendering differs enough between them that the references
would not travel. Reasonable to add on one fixed machine, not as a portable
suite.

**Threading.** The mounting manager asserts it is on the main thread, and text
measurement takes a mutex, but nothing exercises the JS thread and the main
thread concurrently.

**macOS.** CI is Linux only, because that is the target and because every
platform-specific bug so far has been a Linux one. Development happens on a Mac,
so macOS is covered by whoever is working; it is not covered by a machine.

## CI

`.github/workflows/ci.yml`. Installs the dependencies, builds, and runs both
suites under Xvfb with `xdotool` present, so the end-to-end suite is in `real`
input mode.

It builds against a **pinned** React Native commit, in `scripts/react-native.pin`.
This project tracks `main`, which is right for development and wrong for CI: a
build that follows `main` turns every upstream change into a red tick on an
unrelated pull request. A separate `drift` job builds against `main` weekly and
is allowed to fail, so upstream churn is noticed without blocking anything.

Three caches carry the cost: React Native's `node_modules` and `third_party/`
(which holds the Hermes build) are keyed on the pin, and `ccache` on the commit
with a rolling fallback.

They are the difference between a **42 minute** run and a **2 minute** one.
Cold, a run installs React Native's dependencies (949MB of `node_modules`),
builds Hermes, and compiles 400-odd objects of React Native core. Warm, it
restores 304MB of `third_party`, hits ccache, and goes straight to the tests.

Two things about those caches that are easy to lose an hour to:

- **The `third_party` key hashes the whole of `bootstrap.sh`**, so editing a
  comment in it costs a Hermes rebuild. That is deliberate: over-invalidating is
  the safe direction, and the script changes rarely now.
- **A cache saved on a branch is invisible to `main`.** GitHub scopes caches to
  the branch that created them, and shares them only downwards, to that branch's
  descendants. So the first run after merging a branch is cold all over again,
  and only then do the caches land somewhere every later run can reach.
- **`actions/cache` will not save a path that escapes the workspace.** It says
  nothing about it either; the cache simply never appears. React Native is
  therefore cloned to `react-native-src/` *inside* the checkout rather than
  beside it. Getting this wrong is quiet until the day `third_party` hits: then
  bootstrap skips `yarn install` because codegen is already there, and bundling
  fails for want of `node_modules`.
