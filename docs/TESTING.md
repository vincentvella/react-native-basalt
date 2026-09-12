# Testing

Four suites. The two Linux ones need a display, because everything in them is a
GTK program, and both run in CI on Linux, which is where they matter: see
`.github/workflows/ci.yml`.

```bash
./build/basalt_gtk_tests                    # unit
scripts/bundle.sh ../react-native --prod
scripts/integration_test.py         # end to end

./build/basalt_appkit_tests                # the macOS view layer and mounting, on a Mac
./build/basalt_win32_tests.exe             # the Windows view layer, on Windows
scripts/compare_hosts.sh            # both hosts, same script, diffed
```

On a headless machine, run either under a virtual display:

```bash
xvfb-run -a ./build/basalt_gtk_tests
xvfb-run -a scripts/integration_test.py
```

`basalt_gtk_tests` exits 77 rather than failing when it cannot reach a display, which
is the convention for "skipped".

`basalt_appkit_tests` needs neither a display nor a window: nothing in it is presented.
It is built only on a Mac, and CMake omits the target everywhere else.

`basalt_win32_tests` needs neither either, and unlike the other two it makes
real pictures while not needing them: Direct2D renders into a WIC bitmap with no
window, no device and no display connection. That is why the rendering
assertions this project has wanted since GTK live there and nowhere else. Built
only on Windows; CMake omits the target elsewhere.

| File | What it pins down |
|---|---|
| `native/tests/test_win32_view.cpp` | The view layer as a tree. Tags and frames, insert indices, that a Remove detaches without destroying, that an Insert reparents, that destroying a view leaves nothing pointing at it, that zIndex does not touch the child list, and that `describeTree` is byte-for-byte what the other two hosts print. |
| `native/tests/test_win32_paint.cpp` | What reached the pixels. Placement, opacity as a subtree layer, clipping under `overflow`, a quarter turn about the centre *and in the right direction*, zIndex paint order, the scroll offset, and the corner radius. Each is something a tree dump cannot show. |
| `native/tests/test_win32_hittest.cpp` | Picking. Depth, sibling order, zIndex, misses, scroll offsets, hidden views, and that a press follows a `transform` — which this platform has to invert itself, having no toolkit picking to inherit. |
| `native/tests/test_win32_text.cpp` | DirectWrite measurement. Wrapping, `numberOfLines`, that a bigger font measures bigger, that alignment does not change the measured size, and two regression tests carried over from the other hosts: that font sizes are absolute rather than points, and that the default ellipsize mode does not collapse a wrapping paragraph. Plus one they cannot make — that the glyphs actually move when the alignment does. |
| `native/tests/test_win32_image.cpp` | The four resize modes, against a real bitmap and real pixels rather than against the destination rect: `cover` that scales correctly and forgets to clip looks right in a rect and wrong on a screen. Also the WIC decode, round-tripped through the snapshot encoder. |
| `native/tests/test_win32_accessibility.cpp` | The UI Automation mapping, asked the way a screen reader asks it: build the provider and read property ids. Roles, the group fallback, that a plain `<View>` stays out of the tree, labels and hints, the tri-state states, and that a role can change after mount — which GTK cannot do, because a GtkAccessible role is construct-only. |
| `native/tests/test_win32_mounting.cpp` | The mutation walk, through the real Win32MountingManager. Deliberately the same nine questions asked of GTK and AppKit, in the same order with the same tags, so a divergence in the shared walk fails here rather than in an app. Built only when the build was pointed at a React Native. |

## `build/basalt_gtk_tests` — the unit suite

Everything reachable without a JavaScript runtime. No React, no Metro, no
Hermes: mutations are hand-built, the way `mount_harness_gtk` builds them.

| File | What it pins down |
|---|---|
| `native/tests/test_mounting.cpp` | The mutation walk. Create/Insert/Remove/Delete/Update ordering, insert indices, nesting, that Remove detaches without destroying, that Delete unregisters, and that a stray Insert for a deleted tag is ignored rather than fatal. |
| `native/tests/test_view.cpp` | The widget layer. Frames, child order, that a scroll offset moves children, that `measure` reports zero so GTK never competes with Yoga, and that dispose unparents children. |
| `native/tests/test_text.cpp` | Pango measurement. Wrapping, `numberOfLines`, that a bigger font measures bigger, and two regression tests: that font sizes are absolute rather than points, and that the default ellipsize mode does not collapse a wrapping paragraph to one line. |
| `native/tests/test_hittest.cpp` | Hit testing. Depth, sibling order, misses, and that it follows a scroll offset. |
| `native/tests/test_image.cpp` | The image loader. Decoding, the cache answering synchronously, and the three ways a load can fail. |
| `native/tests/test_textinput.cpp` | The controlled-value loop. That applying a prop is not reported back as typing, that the caret survives a prop arriving mid-word, that a command carrying a stale `eventCount` is dropped, and that the `GtkText` peer is allocated inside the content inset. Props are built through React Native's own `RawProps` parser, because the fields that matter are const and only reachable that way. |

## `build/basalt_appkit_tests` — the macOS suite

Two files, sixteen tests, no window and no toolkit initialisation.

`native/tests/test_appkit_view.mm` is against `RnAppKitView` alone, with no Fabric and no
React Native in it. It pins the tag and frame, the child order including
insertion into the middle, that removing a view that is not yours is a no-op,
that background colour, opacity, clipping and corner radius reach the CALayer,
that the background is in sRGB rather than the display's space, and that
`describeTree` emits the same text the GTK side does.

`native/tests/test_appkit_accessibility.mm` asserts the *AppKit* role, not the
React Native name: the name is what `describeTree` reports and what the
cross-platform diff compares, and whether the mapping onto `NSAccessibility`
actually happened is a platform question. The GTK suite's
`test_accessibility.cpp` asserts the same thing about `GtkAccessibleRole`.

`native/tests/test_appkit_input.mm` is hit testing and the touch state machine.
Hit testing is a pure function of the view tree, so it needs no mouse, no window
server, and no permission to synthesise an event -- which matters, because a
real click on macOS means `CGEvent` and accessibility permission an automated
run does not have. Unlike the GTK equivalent none of it needs a window:
`gtk_widget_pick` skips unmapped widgets, so those tests show a window and pump
the frame clock, and `RnAppKitHitTest` walks frames and answers straight away.

`native/tests/test_appkit_mounting.mm` is against `AppKitMountingManager`, and is
deliberately the same questions `native/tests/test_mounting.cpp` asks of GTK, in
the same order, with the same tags and frames. Since both managers now share
their mutation walk, most of what these test is whether that sharing still holds.
Built only when the build was pointed at a React Native; without one the view
tests still run.

The one worth naming is `mac_view_is_flipped_so_frames_are_top_left`. AppKit's
origin is bottom-left and every frame Fabric produces is top-left, so a child at
y=32 in a 420-tall parent belongs 32 from the top; unflipped it would sit at 228
and the layout would look plausible and be wrong.

`native/appkit/mount_harness_appkit.mm` is the mounting equivalent: the same two
transactions `gtk/mount_harness_gtk.cpp` runs, driven through the real mounting
manager with no JavaScript anywhere. `BASALT_SNAPSHOT_DIR=/tmp/out
./build/mount_harness_appkit` writes `mount-1.png` and `mount-2.png` and prints the
tree after each; with no directory set it opens a window and applies the second
transaction two seconds in, the way the GTK harness does.

`scripts/compare_all.sh` runs every app in `js/` through both hosts and prints
where the two platforms stand:

```
  views    identical, frames included
  press    identical, frames included
  scroll   identical, frames included
  image    identical, frames included
  text     identical, frames ignored
  a11y     identical, frames ignored
  input    identical, frames ignored
  index    identical, frames ignored
```

An app that only passes with frames ignored is one whose layout depends on text
measurement. Everything else -- tree shape, strings, colours, roles, flags -- is
still compared there.

`scripts/compare_hosts.sh` is the single-app version, and the one that cannot
run in CI. It runs `js/views.js` -- a real React app made only of `<View>` --
through `basalt_gtk` and `basalt_appkit`, and checks two things that fail
differently: that the view trees match, and that each host's log reports the
`Platform.OS` its bundle was built for. A bundle built for the wrong platform
can render perfectly and be wrong about everything `Platform.OS` guards.

It needs both toolkits installed, so it runs on a developer's Mac rather than on
CI, where each host only exists on its own side. Build the bundles first:

```bash
scripts/bundle.sh --platform linux --entry views.js --out views.linux.jsbundle
scripts/bundle.sh --platform macos --entry views.js --out views.macos.jsbundle
scripts/compare_hosts.sh
```

`BASALT_COMPARE_TAP="x,y;x,y"` forwards taps to both hosts -- they read the same
variable -- so the input path is compared too, not just the initial render.
`js/press.js` is the app for that, and needs a longer quit than the default
since the first tap is at 1500ms:

```bash
BASALT_COMPARE_TAP="400,100;400,100;400,100" BASALT_COMPARE_QUIT_AFTER_MS=5000 \
  scripts/compare_hosts.sh \
  build/press.linux.jsbundle.js build/press.macos.jsbundle.js BasaltPress
```

`js/scroll.js` scrolls itself through `scrollTo` on a ref, which is how both
hosts can be driven identically from one file -- a wheel has to be injected per
platform and a command does not -- and is the only test of the command path:

```bash
scripts/bundle.sh --platform linux --entry scroll.js --out scroll.linux.jsbundle
scripts/bundle.sh --platform macos --entry scroll.js --out scroll.macos.jsbundle
BASALT_COMPARE_QUIT_AFTER_MS=3000 scripts/compare_hosts.sh \
  build/scroll.linux.jsbundle.js build/scroll.macos.jsbundle.js BasaltScroll
```

For `js/text.js` and `js/image.js`, frames have to be ignored:

```bash
scripts/bundle.sh --platform linux --entry text.js --out text.linux.jsbundle
scripts/bundle.sh --platform macos --entry text.js --out text.macos.jsbundle
BASALT_COMPARE_IGNORE_FRAMES=1 scripts/compare_hosts.sh \
  build/text.linux.jsbundle.js build/text.macos.jsbundle.js BasaltText
```

Pango over the system sans and Core Text over San Francisco are different
shapers over different fonts, so the same paragraph is a few points taller on
one than the other and every frame below it shifts. Demanding equality there
would mean the check could never be turned on for text at all. What is still
compared is everything that must match: the tree shape, the strings, the
colours, the clip and opacity flags.

As of phase 23 that comparison differs by exactly one thing -- GTK emits
`role=label` on a paragraph and macOS emits nothing, because macOS has no
accessibility yet.

`native/appkit/demo_layout_appkit.mm` is the visual half of the view layer. It carries the same boxes at
the same coordinates as the GTK `demo_layout_gtk`, so the two can be compared
directly, and `BASALT_SNAPSHOT=out.png ./build/demo_layout_appkit` renders them
offscreen to a PNG. `BASALT_DUMP_TREE=1` prints the tree instead — useful, but
it would print the same thing whether or not the flip works, which is what the
snapshot is for.

## The harness

`native/tests/TestHarness.h`, about sixty lines, no dependencies, shared by all
three. `TestHarness.cpp` is the runner and knows about no toolkit; each suite
brings its own `main` — `tests/main_gtk.cpp` initialises GTK, and the macOS one
sits at the bottom of `test_appkit_view.mm`. Its
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

This is what `BASALT_DUMP_TREE` exists for. The path this project exists to
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
5. **Fast Refresh.** Starts its own Metro on port 8099, runs the host in dev
   mode against it, edits `js/index.js` while the app is on screen, and checks
   the new text is in the tree the host dumps. This is the only scenario that
   runs in dev mode at all, which is why it exists: "development still works"
   was being taken on trust, and a wrong claim about it reached the README.

   It needs a React Native checkout. `scripts/metro.sh` looks for one beside
   the repo; set `RN_DIR` if it is somewhere else, as CI does.

   It edits a tracked file, and restores it after the host has exited — not
   before, because putting the original back while the app still has a Metro
   connection triggers a second refresh that undoes the edit before the tree is
   dumped. That failure looks exactly like Fast Refresh being broken. A `kill
   -9` of the test would leave the demo edited; nothing softer will.

   **It does not run in CI.** Metro on a GitHub runner never notices an edit:
   the file changes on disk with a fresh mtime, a newly requested bundle still
   carries the old text, and Metro's log is empty of complaint. `fs.watch` sees
   the same edit on the same runner, the inotify limits are generous, both
   sides run the same Node, neither has watchman, and CI's directory layout
   reproduces green in a local VM — so it is the runner, not this repo, and
   chasing it further was costing more than it was worth. `BASALT_SKIP_FAST_REFRESH`
   turns it off and CI sets it; the scenario reports itself as skipped rather
   than quietly passing. It does run, and pass, on macOS and on Linux in a VM.
   `plan/backlog.md` lists what to try next, starting with installing watchman
   on the runner, since both sides currently fall back to the same node watcher
   and that is the part under suspicion.

   It rewrites the edit every ten seconds until the refresh arrives. That is
   not paranoia: a file watcher that has not finished attaching does not queue
   anything, the event is simply never delivered, and nothing re-crawls. Metro
   watches the React Native checkout, some eight thousand directories, and that
   takes markedly longer on a cold CI machine than on a warm laptop. Everything
   cheaper was ruled out first — `fs.watch` sees edits on the runner, its
   inotify limits are generous, and neither CI nor a development machine has
   watchman, so both run the same node watcher.

   It waits on the host's log rather than on sleeps, and asks the host to quit
   with `SIGTERM` once the refresh has landed. The first version used a fixed
   budget and failed in CI, where Metro is cold and a rebuild takes longer than
   a developer's warm one. Two things it needs to know before it edits
   anything, both learned the hard way: that the app is up, and that the bundle
   it is running came from Metro. The host falls back to the on-disk bundle
   when Metro is slow to answer, and that bundle is a production one, so an
   edit would never reach it — reported, unhelpfully, as Metro never pushing an
   update. So the scenario builds the bundle over HTTP before starting the
   host, and then confirms a `__DEV__` bundle is what evaluated by watching for
   the LogBox TurboModule request that only a dev bundle makes.

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

**`injected`** — `BASALT_TEST_TAP` calls the gesture callback directly,
skipping GDK. The fallback where a real event cannot be synthesised, which
notably includes macOS: doing it there needs accessibility permission an
automated run does not have.

Force either with `--input real` or `--input injected`.

Typing splits the same way. In `real` mode `xdotool type` sends key events
through the X server, so GDK and the input method see them. In `injected` mode
`BASALT_TEST_TYPE` inserts through `GtkEditable` on whatever field has focus,
which skips the key controller and the input method and exercises everything
above them.

## Running on Linux

The project targets Linux and is developed on a Mac, so everything here should
be checked on Linux before it is believed. A VM is enough:

```bash
brew install lima
limactl start --name=basalt <a template with vz, 8 cpus, 16GiB, 120GiB>
limactl shell basalt
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
WAYLAND_DISPLAY=wayland-1 GSK_RENDERER=gl ./build/basalt_gtk build/main.jsbundle.js
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
