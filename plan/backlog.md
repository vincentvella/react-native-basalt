# Backlog

Not scheduled. Roughly by value.

## Windows

Since phase 47 Windows is a peer rather than a port in progress. It mounts every
component the other two desktops do -- `<View>`, `<Text>`, `<Image>`,
`<ScrollView>` and `<TextInput>` -- and Hermes evaluates a bundle, Fabric diffs
a shadow tree, Direct2D paints it in an HWND, a click on a `<Pressable>` runs
its `onPress`, a wheel scrolls a list, and a character typed into a field
reaches React and comes back. It runs the same end-to-end suite, it has a
`run-windows`, and `compare_hosts.sh` knows how to run it.

Struck-through entries below are what that took, kept because the order they
were done in is the useful part. What is not struck through is real and named,
and none of it is a missing half.

- ~~**No mounting manager.**~~ Phase 42. Phase 19 had already done the expensive
  part: the mutation walk is portable, so what Windows owed was seven operations
  and a props translation, and `core/MountingWalk.h` was not modified.
- ~~**React Native's C++ core has never been compiled with MSVC.**~~ It has, in
  phase 41: 442 objects, `basalt_core.lib`, every translation unit in
  ReactCommon, ReactCxxPlatform, folly and Yoga under clang-cl. What is left of
  it is below.
- ~~**Hermes does not build from source on Windows.**~~ It does, with three
  fixes, and it had to: the prebuilt everyone else consumes cannot be used here.
  `Microsoft.JavaScript.Hermes` ships jsi headers with no `IRuntime` at all and
  React Native 0.87's have 186 references to it, so every JSI symbol mangles
  differently. That is structural rather than bad luck -- react-native-windows
  targets React Native 0.84 and react-native-macos 0.81, and this is on 0.87, so
  nobody has built the prebuilt yet. `supported-versions.json` says Windows is a
  platform with a prebuilt Hermes; that is true in general and false for any
  React Native newer than the forks, and it should say so.
- **The Hermes patch is applied by hand and nothing reapplies it.**
  `HERMES_EMPTY_BASES` has to be added to `VM::Environment` in
  `include/hermes/VM/Callable.h`, and `third_party/hermes` is a downloaded
  tarball that `bootstrap.sh --force` deletes. Until bootstrap applies it, a
  fresh checkout gets a Hermes that fails on a static assertion. Upstream should
  take this: the macro is defined in `Support/Compiler.h` for exactly this
  purpose, with a comment naming `HERMESVM_CONTIGUOUS_HEAP`, and is applied to
  nothing in the entire tree.
- **React Native's own warnings are not enforced on Windows.** Its CMake applies
  `-Wall -Werror -Wpedantic`, all of which clang-cl either misreads or takes
  from an INTERFACE property that lands after anything that could counteract
  them, so phase 41 strips them. Linux stays the build that guards upstream
  warning cleanliness. Restoring them properly means `/clang:-Wall` or an
  equivalent that survives the link graph, and has not been attempted.
- ~~**`native/bootstrap.sh` has no Windows path.**~~ It has one, and it was run
  end to end: it detects Git Bash, finds a vcpkg that actually has the packages,
  patches Hermes, builds it with the three Windows flags, and prints a configure
  line that works. What it does **not** do is install anything -- vcpkg and its
  packages are still a manual prerequisite, and so is a Node new enough for
  React Native 0.87, which wants `^22.13 || ^24.3 || >= 26` and is not what a
  machine is likely to have.
- ~~**Nothing has run bootstrap from an empty `third_party`** on Windows.~~
  Done while writing the full Windows CI job, which would have been the first
  thing to do it -- and the entry was right to worry. Every earlier bootstrap
  had found Hermes already built by hand, so its configure step had never run,
  and it failed on its first real execution: Git Bash's MSYS runtime rewrites
  an argument that looks like an absolute POSIX path, `/DWIN32` looks exactly
  like one, and clang-cl was handed `C:/Program Files/Git/DWIN32`. The fix
  excludes that one argument from conversion rather than all of them, because
  the `-S` and `-B` paths beside it are only correct *because* of the same
  conversion; turning it off wholesale was the first attempt and broke those
  instead. From empty it now fetches, patches and builds Hermes, installs yarn
  and runs codegen with no errors.
- ~~**`<Text>`**~~ (phase 40, over DirectWrite) and ~~**`<Image>`**~~ (phase 40,
  over WIC, with the fetch shared from `core/ImageBytes.cpp`).
- ~~**Accessibility.**~~ Phase 40, over UI Automation -- the one place Windows
  is structurally different rather than differently spelled, since UIA wants a
  provider object where GTK and AppKit take properties on a view. What is left
  of it is `IRawElementProviderFragment` and the control patterns, both of which
  need the HWND fragment root and so belong with a later phase of the host.
- ~~**No input at all.**~~ Phase 44. `Win32TouchDispatcher` turns
  `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` into React Native touches, and
  `js/press.js` counts presses on Windows. What is still missing is what a mouse
  has and a finger does not: **no right button**; **no keyboard**, which arrives
  with `<TextInput>` because there is nothing yet that focus could belong to. The
  wheel is not among them any more -- phase 45 took it, and routed it through
  the mounting manager rather than the touch dispatcher, because what it needs
  is the list of tags that are ScrollViews.
- **`offsetPoint` is page coordinates on all three platforms.** A touch should
  carry its position relative to the view it hit, and instead carries it
  relative to the surface root. Pressability does not read it, which is why
  nothing has noticed. On Windows the fix is cheap and was left out for
  symmetry: the hit chain the dispatcher already builds for gestures carries
  exactly the origin needed, so it only has to be built for touches too --
  which means paying for it on every press rather than only when RNGH is
  attached.
- ~~**No `<ScrollView>` on Windows.**~~ Phase 45. What it does not have is
  scrollbars, momentum, and a precision touchpad -- a Windows touchpad reports
  through `WM_POINTER*` rather than `WM_MOUSEWHEEL`, so a two-finger scroll
  arrives as notches like a wheel rather than as pixels, which neither other
  desktop confuses. `SPI_GETWHEELSCROLLLINES` is ignored on purpose so that one
  notch moves the same distance on all three.
- ~~**No `<TextInput>` on Windows.**~~ Phase 46, over a real `EDIT` control.
  Windows now mounts all five components the other two desktops do. What it
  does not have is multiline, `onKeyPress`, `onSelectionChange`, a tab order, or
  the colours the cue banner and the selection take from the system -- and a
  field with no `backgroundColor` paints the system window colour, because a
  child window cannot see through itself to what Direct2D drew behind it.
- **`BASALT_SNAPSHOT` cannot see a `<TextInput>` on Windows.** It renders the
  `RnWin32View` tree offscreen and a text field's peer is a child window, not a
  view -- so a field comes out as its background with no text, no placeholder
  and no caret. Not a bug in the snapshot, but it does mean the one host that
  can assert on pixels cannot assert on the one component whose appearance is
  hardest to get right. `PrintWindow` on the live window is the answer and
  nothing in the repository does it yet.
- **A `<TextInput>` on Windows is always on top of everything.** Its peer is a
  child window, so a later sibling cannot cover it and it does not clip to a
  scrolled ancestor -- it is hidden when it leaves the ancestor's box instead,
  which looks right until something is half-scrolled. A transform on the view
  does not reach it either: a window cannot be rotated.
- **The Windows choreographer is a 16ms timer**, not a display link. Doing it
  properly means `DwmGetCompositionTimingInfo` and `DwmFlush` on a thread of its
  own, because `DwmFlush` blocks and a blocked UI thread is worse than a
  slightly wrong interval. The same gap already recorded for worklets and
  Reanimated on both other desktops.
- ~~**Text colour is per-paragraph, not per-run.**~~ Phase 47. DirectWrite
  carries colour as a drawing effect rather than as a range attribute, which
  looks like it needs a custom `IDWriteTextRenderer` -- but Direct2D's own
  renderer has exactly one special case, and a drawing effect that is an
  `ID2D1Brush` is it.
- ~~**No `react-native run-windows`.**~~ Phase 47, and `run-macos` with it. All
  three are one function in `react-native-basalt/cli/desktop.js` with four
  strings passed in, which is the same split the C++ half makes.
- ~~**CI has no Windows runner.**~~ Added in phase 39, and small: the view layer
  builds with MSBuild and needs no display, no React Native and no bootstrap.
- ~~**No machine has run `scripts/compare_hosts.sh` with Windows in it.**~~ It
  has, once WSL2 worked: the GTK host built in Ubuntu 24.04 by
  `scripts/wsl_setup.sh`, beside the Windows host, and `compare_all.sh` says the
  two agree on all twelve apps. Getting there found two things, neither of them
  in a view layer. The distro had nothing registered for `https`, so Linux
  honestly failed a `canOpenURL` check a GitHub runner passes -- an environment
  gap, now covered by the setup script. And Windows failed a clipboard check in
  2 runs of 15, because a refused `OpenClipboard` silently dropped the write.
  It retries now, with a test that holds the clipboard from a window and fails
  without the retry; what was holding it was never caught, and 190 runs with
  logging on every refusal saw none.
- **`scripts/integration_test.py` skips Fast Refresh on Windows**, because
  `scripts/metro.sh` is a shell script. That reason has gone. A development run
  goes through `run-windows`, which starts Metro itself, and Fast Refresh works
  on Windows now -- an edit to a running Expo app reaches the tree. It did not
  before, for three separate reasons: the host read its Metro entry from an
  argument nothing passed, React Native's `start` command needs
  `@react-native/metro-config` in the app, and every host reported a dev script
  URL naming `linux`, so Metro sent Windows HMR updates for a Linux graph. The
  scenario should start Metro the way the run command does and stop skipping.
  The other four scenarios run.
- ~~**CI does not build the Windows host.**~~ It does, as `windows-full`:
  vcpkg, Hermes from source, React Native's core under clang-cl, every Windows
  test, the demo bundle and the end-to-end suite. Green on its first run, and
  51 minutes cold -- vcpkg 20, Hermes 13, the build 17. What that cost is
  what the caching around it is for: every cache is now saved by its own step
  as soon as it exists rather than when the whole job succeeds, the Windows
  build goes through ccache, and a change to `bootstrap.sh` no longer throws
  away Hermes. Pushes that touch only prose start no jobs at all.
- ~~**CI had been red for twenty-three commits.**~~ Fixed in phase 47, and worth
  keeping written down. The last green run was "Blob, File and FileReader"; the
  Linux job failed on every commit after it, including all nine Windows phases,
  and nobody looked. Both causes were invisible from a Windows desk -- a static
  archive cycle only GNU ld minds, and a header reaching `<cstdint>` through
  windows.h -- which is the argument for `scripts/check_includes.js` and for
  reading the log rather than the tick.
  - **Nothing makes a red build hard to ignore.** No branch protection, no
    required check, no notification anyone reads. Thirteen commits is what that
    costs, and none of the work above changes it.

## macOS

- **Hit testing ignores `transform`.** `RnAppKitHitTest` walks `child.frame` and
  never consults the layer transform, so a rotated view is clickable where it
  used to be and not where it is drawn. GTK does not have this bug, because it
  composes the transform into the widget's allocation and `gtk_widget_pick`
  follows -- which is the reason `plan/decisions.md` gives for putting it there
  rather than in the paint. Windows does not have it either, because it inverts
  each view's matrix on the way down. Found in phase 40 while writing the
  Windows equivalent; not fixed, because there is no Mac to check a fix on. The
  test that would catch it is `hit_test_follows_a_transform` in
  `packages/react-native-basalt-win32/native/tests/test_win32_hittest.cpp`.
- **No accessibility.** AppKit views carry no role, so a screen reader sees a
  tree of untyped views. It is also the only thing `scripts/compare_hosts.sh`
  finds different between the two hosts on a text-heavy app: GTK emits
  `role=label` on a paragraph and macOS emits nothing.
- **Justified text.** `NSTextAlignmentJustified` reaches the paragraph style and
  Core Text ignores it for lines drawn individually, which is how RnTextLayout
  has to draw them to honour `numberOfLines`. Doing it properly needs
  `CTLineCreateJustifiedLine` per line.
- **Fonts loaded at runtime are untested.** `resolveFontFamily` is wired into
  the Core Text font lookup, and `expo-font` on macOS has never been run end to
  end.
- **No `maxLength` on macOS.** An NSTextField has no maximum length; enforcing
  one needs a formatter or a delegate that rejects edits. GTK gets it from
  `gtk_text_set_max_length`, so the two behave differently.
- **No multiline `<TextInput>`** on either platform. It wants an NSTextView in a
  scroll view on macOS, and a GtkTextView on GTK -- a different peer either way.
- **No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`** on
  macOS, all of which AppKit has some form of.
- **Nothing tested against a real screen reader**, on either platform.
  VoiceOver and Orca are both a manual step nobody has taken; the unit tests
  assert the properties were set and cannot assert the result is usable.
- **No accessibility subroles.** A search field should be a text field with
  `NSAccessibilitySearchFieldSubrole`; reporting only the role loses the "this
  searches" part.
- **`accessibilityValue`, `accessibilityLiveRegion` and
  `accessibilityLabelledBy`** are unimplemented on both platforms.
- **No animated images.** The first frame of a GIF is drawn as a still, on both
  desktops.
- **No scrollbars.** AppKit's are `NSScroller`, which comes with
  `NSScrollView`, so an overlay indicator is real work rather than a property.
  The GTK side gets them from its widget theme.
- **No scroll momentum or elasticity.** A trackpad flick stops dead, which is
  visibly un-Mac-like. `onMomentumScroll*` never fire, as on GTK.
- **Nothing is reachable by Tab**, on either platform: no tab order is
  implemented, so a field can be clicked into and not tabbed into. A focused
  field does take keyboard input, as of phase 28.
- **No gesture cancellation from the platform.** `dispatchTouchCancel` exists
  and nothing calls it: AppKit has no equivalent of GTK's gesture `cancel`, and
  the case it covers -- a press interrupted by the window losing focus -- has no
  handler yet.
- Within `<View>`: per-corner radii, borders, transform, z-index and
  pointer-events are unmapped. The GTK side has all of them.

## Testing

- **No rendering assertions on GTK or macOS.** The widget tree says a view has a
  colour and a frame, not that the right pixels reached the screen. This is not
  theoretical -- GTK's cairo renderer mangled every transform in the demo and no
  test noticed. See `docs/TESTING.md`.

  Windows has them as of phase 39, because Direct2D renders offscreen with no
  window and no display and the other two toolkits do not. So the question is no
  longer what to assert -- `tests/test_win32_paint.cpp` is the list -- but what
  each of the others costs: a display server and a `GdkTexture` read-back on
  GTK, an offscreen `NSWindow` and a display cycle on macOS. The macOS snapshot
  path in `AppKitSnapshot.mm` already does the hard half of the second one.
- Nothing exercises the JS thread and the main thread concurrently.
- The end-to-end scenarios hard-code tap coordinates from the demo's layout.
  Finding a button by its label in the dumped tree would survive a restyle.
- CI builds and tests Linux and Windows on every push. macOS is built only by
  `release.yml`, which has not run yet, and otherwise by whoever is developing
  on a Mac.
- **The Fast Refresh scenario is skipped in CI**, so nothing on a machine
  protects development mode. Metro on a GitHub runner never notices an edit:
  the file changes on disk with a fresh mtime, a newly requested bundle still
  carries the old text, and Metro logs nothing. Ruled out already: `fs.watch`
  sees the same edit on the same runner; the inotify limits are 655360 watches
  and 1280 instances; both sides run Node 24.20.0; and CI's layout, with React
  Native inside the checkout, reproduces green in a local VM. The scenario
  passes on macOS and on Linux in a VM, so this is about the runner rather than
  the code.

  **Watchman was the standing theory, and it is now ruled out.** It was tried
  on the `ci` branch and did not fix the scenario. What the run established,
  so that none of it needs doing again:

  - The upstream build was used, not Ubuntu's 4.9.0 from 2017, so a failure
    here says something about the theory rather than about an ancient client.
    It has to come from a GitHub release: watchman is not in apt, and
    facebook/watchman stopped attaching release assets after v2026.07.27.00,
    which is the last version that ships a Linux binary at all.
  - It ran. `watchman --version` answered `20260727.012849.0`, and
    `watchman watch-project js` reported `"watcher": "inotify"` over exactly
    the root Metro was given.
  - A `.watchmanconfig` was added at the project root -- React Native ships one
    and this repo never had it -- so watch-project resolved the root Metro
    asked about rather than some parent. That file is still in the repo, since
    it is correct regardless of CI.
  - The scenario failed the same way, and `serves_edit` answered **no**: a
    freshly requested bundle still carried the old text, so Metro had not seen
    the change at all.

  What that leaves is the one thing the run did not establish: whether
  *metro-file-map* used watchman, or found it and fell back anyway. The
  `watch-project` call above was made by the workflow, not by Metro, so a watch
  existing proves nothing about which watcher Metro chose.

  Next thing to try, in order of effort:

  1. **`DEBUG=metro:*` on the CI Metro**, to see what the watcher thinks it is
     doing rather than inferring it from what the bundle contains -- starting
     with whether it picked watchman at all, which is the question the run
     above left open.
  2. Shrink `watchFolders`. The React Native checkout is the bulk of the eight
     thousand directories; if the watcher is falling over on volume, a narrower
     watch would show it.
  3. Have the scenario poll Metro for the edit rather than waiting on the host,
     which would at least separate "Metro never saw it" from "Metro saw it and
     the client missed it" without another CI round trip per hypothesis.
- **The GTK `<TextInput>` focus scenario flaked on a Mac, and nothing explains
  it.** `focus a TextInput, type, and see it round-trip through React` failed
  three runs in a row on 2026-09-13 with *"the focus command did not move focus
  to the field"*, then passed six in a row, on the same build and the same
  machine. No change was made between the two states that touches focus.

  It was running the **GTK host on macOS**, over the quartz backend, which
  `docs/HANDOFF.md` already warns is not the target: keyboard focus there is
  the window server's to give, and the scenario needs the window to have it
  before `TextInput.focus()` can mean anything. CI, on real Linux under Xvfb,
  was green across the whole period and has never reproduced it.

  What was ruled out: it is not a code change. The one edit in flight touched
  `main_gtk.cpp` and was reverted, rebuilt and re-run, and the failure
  survived that -- so it was already failing before anything that day touched
  the host. The suspicion is that a pile of stray `basalt_gtk` and
  `basalt_appkit` processes from earlier runs were holding or stealing focus,
  since killing them is the only thing that happened between the last failure
  and the first pass. That is a correlation and nothing more; it was not
  tested by reproducing it.

  Worth doing before trusting it: reproduce deliberately -- leave a host
  running and start another -- and if that is it, have the scenario fail with
  "another host is already running" rather than with a focus error, which is
  the misleading half. If it cannot be reproduced that way, the next suspect
  is the quartz backend itself, and the answer is that this scenario should
  not be believed off a real Linux session at all.

- CI has no rendering assertions, so it cannot catch what the cairo renderer did.

## Compatibility

Found by bundling and running a real application; see `plan/10-first-real-app.md`.

- **React Native 0.82 and older cannot reach a native module.** The host
  installs `global.nativeModuleProxy` but not `global.__turboModuleProxy`, and
  relies on `TurboModuleRegistry` falling back to `NativeModules`. On `main` and
  on releases from 0.83 that fallback is unconditional; on 0.82 and earlier,
  which includes kino's 0.81.6, it is gated behind
  `RN$Bridgeless !== true || RN$TurboInterop === true ||
  RN$UnifiedNativeModuleProxy === true`, all of which are false here. So every
  `getEnforcing` fails on those versions, starting with `PlatformConstants`.
  Setting
  `globalThis.RN$TurboInterop = true` before the bundle evaluates fixes it and
  is verified; installing `__turboModuleProxy` is the better answer and belongs
  upstream in ReactCxxPlatform. 0.87.1 works without any of this, so the
  supported range starts there; see `plan/11-released-versions.md`. Extending
  it downwards means testing each version, not just setting the flag.
- React Native 0.83 through 0.86 are refused rather than untested-but-allowed.
  Each would need building against and both suites run; see
  `plan/14-pinned-versions.md` and `supported-versions.json`.
- Supporting React Native 0.81, which is what kino pins, needs three separate
  things: `RN$TurboInterop` so any native module resolves, a Hermes whose CMake
  names its target `hermes` rather than `hermesvm`, and then Expo's native
  runtime before kino itself would start. It is a decision, not a bug.
- **CI checks one React Native per run.** It pins 0.87.1, and `main` moved to
  the weekly drift job, which does not block. So a regression that only affects
  `main` can wait up to a week. Building both on every run would be the fix and
  would double CI cost on a private repo.
- **Expo starts but no Expo module works.** `globalThis.expo` is installed now, from the
  app's own expo-modules-core, so an Expo app starts and renders. What does not
  exist is any Expo *module*: `ExpoAsset` and `ExponentConstants` are stubs that
  exist only so Expo's start-up survives, and `expo-font`, `expo-image` and
  every config plugin's native half are each their own port.
- No `@shopify/react-native-skia`, which is not this platform's to fix, but is
  worth knowing as the thing that stops one real app dead.

## Correctness gaps in what exists

- `borderStyles` — dashed and dotted borders. GTK's border node paints solid
  only, so these need a custom path.
- `backfaceVisibility` — needs the transform's determinant sign at paint time.
- `pointScaleFactor` — fractional scaling under Wayland.
- `transformOrigin` is passed through but never exercised; the default centre
  anchor is.
- 3D transforms have no perspective: `gsk_transform_perspective` exists, and
  `Transform` carries the matrix, but nothing sets it up.

## Host wiring

- `Scheduler::reportMount` is never called. It only drives mount hooks (perf
  tooling, Fantom's test observation), so nothing renders wrongly without it,
  but a real host reports. Needs the mounting manager to hold a
  `SchedulerTaskExecutor`, as `TesterAppDelegate` does.
- `IDevUIDelegate` / LogBox: JS errors currently go to `g_warning` and nothing
  else, so a mistake in an app is a log line and a window that keeps sitting
  there. Cheaper than it sounds: ReactCxxPlatform already *has* a `LogBox`
  module and only declines to hand it over because this host passes a null
  `logBoxSurfaceDelegate`. The work is supplying that delegate and a surface to
  render into, most likely a second GTK window, not writing an error overlay.
- **Dev support is tied to the dev server, so an offline `__DEV__` bundle cannot
  run.** `ReactCxxTurboModuleProvider` serves `DevSettings` only when a
  `DevServerHelper` exists, and LogBox reads that module at import time, so a
  `--dev` bundle loaded from disk throws before React renders. Android and iOS
  do not work this way: a debug build has dev support whether or not Metro is
  reachable. It costs nothing today because development goes through Metro,
  where it works; it would matter for a debuggable build shipped without a
  packager. Fixing it means providing the module ourselves rather than relying
  on ReactCxxPlatform's condition.
- TurboModules the demo's own JavaScript asked for and did not get, none fatal
  today: `BlobModule`, `DeviceEventManager`, `SoundManager`, `IntentAndroid`,
  `RedBox`, `ReactDevToolsSettingsManager`. That is a record of one run rather
  than a list of what to build; for the APIs this platform actually owes an
  implementation, see "Core modules this platform does not provide" below.
- `src/LinuxNetworking.cpp` supports only string request bodies. Blob, form-data
  and base64 need a Blob implementation first.
- The `linux` platform redirects nine React Native shims to their `.android.js`
  siblings. Each is a place this platform could diverge, and a place upstream
  could change under it; only `Platform` diverges today.
- Nothing checks that the shim list in `metro-config.js` still matches
  React Native. A new shim upstream shows up as an undefined export at runtime.

## Core modules this platform does not provide

ReactCxxPlatform supplies fourteen TurboModules: `Animated`, `AppState`,
`DeviceInfo`, `DevLoadingView`, `DevSettings`, `ImageLoader`, `LogBox`,
`ExceptionsManager`, `IntersectionObserver`, `MutationObserver`, `Networking`,
`PlatformConstants`, `SourceCode` and `WebSocket`. Everything below is a React
Native API with no implementation anywhere in this stack, and each one is ours
to write against GTK, GLib or the portals.

How each fails matters, and splits in two. Most are looked up with
`TurboModuleRegistry.get`, which returns null, so React Native's JavaScript
falls back or silently does nothing -- the API appears to work and simply has no
effect. `Clipboard` and `Vibration` use `getEnforcing`, which throws at import,
so anything importing them dies at startup.

- ~~**`Appearance`**~~ **done in phase 30**, on both desktops. What is left of
  it: live system switching is subscribed to and has never been watched firing,
  there is no `AppearanceProvider` or per-view override, and nothing native
  repaints on the switch because neither platform has a `PlatformColor` or a
  system control to repaint.
- ~~**`BlobModule`**~~ **done in phase 31**, on both desktops. What is left:
  blob request bodies and `responseType: 'blob'`, both blocked upstream (below),
  binary websocket frames, and `readAsText` understanding only UTF-8.
- ~~**`Clipboard`**, **`Vibration`**, **`AlertManager`**, **`LinkingManager`**,
  **`I18nManager`**, **`AccessibilityInfo`**~~ **done in phase 32**, on both
  desktops. What is left: alert prompts on Linux (GtkAlertDialog has no text
  field), `login-password` alerts on either, incoming URLs, reading another
  application's clipboard on Linux, and `setAccessibilityFocus` /
  `announceForAccessibility`, which need a per-view accessibility handle neither
  mounting manager exposes.
- **React Native's JavaScript branches two ways and a third platform lands on
  iOS.** `TextInput.js` (phase 09), `ImageViewNativeComponent`'s view config
  (phase 26) and `AccessibilityInfo.js` (phase 32) all did this, each found by an
  app failing rather than by review. Worth checking for deliberately the next
  time a module misbehaves.
- **`ShareModule`** (soft) is the core-module gap left. `ToastAndroid` and
  `DevSettings` were also throwing and are done; a toast logs rather than
  showing, because a desktop's nearest equivalent is a system notification and
  neither platform has a notification API wired up.
- **A desktop notification API**, which is what `ToastAndroid` should really be
  and what any app wanting to tell a user something out-of-band needs.
- **A Metro error still has no red box.** Phase 33 stopped the error page being
  compiled as JavaScript and prints Metro's own message, which is most of the
  value, but the host exits rather than showing it. An app already running when
  a reload fails is a separate case, and it currently keeps running the code it
  has, with the error only in the log.

## Expo, beyond the template

Measured in phase 34 against a real dependency set, on both desktops.

- **expo-image's decorative props**: placeholder, transition, blurhash, cache
  policy. Left out of the view config in phase 36, so they are dropped in
  JavaScript and an app gets an image without them.
- **More Expo views.** The seam exists now (phase 36), so expo-linear-gradient,
  expo-blur and the rest are each a props class, a descriptor and a mounting
  peer rather than a new mechanism.
- **A URL delivered to a running app.** `ExpoLinking.getLinkingURL` is honestly
  null because neither desktop can receive one: macOS needs an Apple Event
  handler and a registered scheme, Linux a desktop entry and single-instance
  activation. The Expo and React Native modules for it both exist now.
- **The clipboard's image and URL types**, which are separate pasteboard types
  on each platform. Absent rather than stubbed, so expo-clipboard reports them
  as unavailable by name.
- **A frame source the animation systems can share.** Worklets and Reanimated
  both run on a sixteen-millisecond timer, because each platform's display link
  belongs to React Native's `AnimationChoreographer` and pauses whenever React
  Native has no animation of its own.
- **`core/ReanimatedCompat.h` has never been compiled.** It exists for a
  platform that is neither Android nor Apple; the Linux host here is built on
  macOS. The first real Linux build is its first test.
- **Reanimated's layout animations and shared element transitions** compile and
  have never been run.
- **`synchronouslyUpdateUIProps`**, Reanimated's direct-to-view path, is a
  no-op: the mounting managers accept mutations only from a Fabric transaction.
- **The gestures a cursor cannot make**: pinch, rotation and force touch begin
  and fail, because there is no second finger and no pressure. Two-finger
  trackpad gestures exist on both desktops and are not wired to anything.
- **The rest of RNGH's relation graph**: `blocksHandlers`, and a `waitFor` that
  resolves across detectors rather than within one gesture.
- **A visible error when the bundle throws.** Today the window stays empty and
  the only evidence is a line in the host's log. Every failure above, and every
  future one, is invisible to whoever is running the app.

## Desktop capabilities

React Native has no cross-platform API for any of this, because it was built for
phones. That makes each one a design question before it is an implementation
question: invent a `react-native-basalt` API, follow what react-native-macos or
react-native-windows already chose, or leave it to userland modules. Nothing
here has been decided, and none of it is needed for the demo, which is why it
has gone unrecorded until now.

- **More than one window.** The host creates exactly one and mounts one surface
  in it. Fabric supports multiple surfaces; nothing above it does.
- **Window size, position, fullscreen and close behaviour**, none of which an
  app can influence. The title can be, on Windows, with the title bar's colours
  and a hidden style that lets the app draw its own header -- `useTitleBar`,
  `<TitleBar>`, `<TitleBar.DragRegion>` and `useTitleBarMetrics` in
  react-native-basalt. Linux and macOS ignore those calls until their hosts
  implement them.
- **Menus**, both a menu bar and context menus.
- **Native file dialogs.** `GtkFileDialog` exists; nothing exposes it. Note that
  kino's own macOS module ships a folder picker, so this is what a real app
  reaches for early.
- **Drag and drop**, in and out of the application.
- **A system tray icon**, and desktop notifications.
- **Cursor control** beyond what the `cursor` style property covers.

## Input

- ~~No hover.~~ Done on all three hosts. `onPointerEnter`, `onPointerLeave`,
  `onPointerOver`, `onPointerOut` and `onPointerMove` fire, and `js/hover.js`
  plus a scenario in the end-to-end suite prove it. Worth recording what the
  work turned out to be, because the obvious implementation is wrong: a host
  emits `pointerMove` and nothing else. `PointerEventsProcessor` in ReactCommon
  already keeps the hover path and derives enter, leave, over and out from a
  single move, with the listener filtering and the capture rules. Emitting them
  from a platform as well produces each one two or three times over -- see
  core/HoverTracker.h, which is now only the gate that keeps an app with no
  hover listeners from paying for a JavaScript round trip per motion event.
  What is still missing is `onMouseEnter`/`onMouseLeave`, which are not React
  Native core props at all: they exist only in the macOS and Windows forks.
- ~~No momentum scrolling.~~ `onMomentumScrollBegin` and `onMomentumScrollEnd`
  fire on macOS and Linux, and the two platforms need opposite amounts of work
  for it. macOS decelerates a scroll itself and puts a `momentumPhase` on the
  event, so the AppKit host reports what it is handed. GTK emits one
  `decelerate` signal carrying the velocity the gesture ended at and then
  stops, so the coasting is modelled here -- core/ScrollMomentum.h, exponential
  friction at React Native's own `decelerationRate`.

  Windows has neither. A precision touchpad reports `WM_MOUSEWHEEL` with fine
  deltas and no fling: inertia there belongs to Direct Manipulation, which
  wants to own the viewport. So a Windows fling coasts no further than the
  fingers took it, and the two events never fire. Driving the shared model off
  a wheel would be worse than silence -- a wheel is not a throw.
- `setIsJSResponder` is a no-op. It matters once something scrolls natively, so
  it lands with `ScrollView`.
- `Touch::offsetPoint` carries page coordinates rather than coordinates relative
  to the target view. Pressability does not read it; anything doing its own hit
  maths would.
- No keyboard focus model outside `<TextInput>`, which takes focus only because
  GtkText does. Nothing else is reachable by Tab, and no key events are
  emitted. A desktop application that cannot read a keypress is not really a
  desktop application; note that React Native has no cross-platform key event
  API to be compatible with, so this needs a decision as well as an
  implementation.
- `PanResponder` works, verified with real pointer motion through an X server
  against a probe that drags a view. Worth stating because it was never
  deliberately built, and a real app uses it for every drag it has.
- Multi-touch is not modelled: one pointer, identifier 0.
- Wayland input is unverified. Rendering is checked on Wayland and input on
  X11, but not both at once: a headless compositor has no seat, so there is no
  pointer to move. Needs a desktop session or real hardware.

## Image

- Nothing evicts the texture cache. A long-lived app that scrolls through many
  remote images grows without bound.
- `resizeMode: 'repeat'` falls back to `center`; a repeating draw needs a
  pattern node rather than one texture append.
- `blurRadius`, `tintColor`, `overlayColor`, `fadeDuration` and
  `progressiveRenderingEnabled` are ignored.
- Assets are never fetched over the network, so a dev server's assets do not
  work; `downloadAsync` rejects saying so. The host has an http client already.
- Nothing caches a downloaded asset, which is right for a local file and will
  not be for a remote one.
- `onProgress` and `onPartialLoad` are never emitted.
- `IImageLoader` itself is still unimplemented, so `Image.getSize` and
  `Image.prefetch` do nothing. That is a separate seam from the rendering path.

## ScrollView

- Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11.
- No momentum, so `onMomentumScrollBegin` and `onMomentumScrollEnd` never fire
  and `onScrollEndDrag` reports zero velocity. `ScrollView._isAnimating()` is
  wrong as a result.
- `animated: true` scrolls instantly.
- No snapping, paging or `maintainVisibleContentPosition`.
- No scrollbars are drawn.
- `contentBoundingRect.origin` is assumed to be zero; iOS positions its
  container view at that origin.
- `disableViewCulling` is never set, which will matter once AT-SPI lands.

## Text

- Inline views (`<Text><View/></Text>`) measure as zero-sized attachments.
  Doing it properly means `PangoAttrShape` placeholders sized from the child's
  own measurement, and returning their rects from `measure`.
- No baseline, so `alignItems: 'baseline'` is wrong for text.
  `pango_layout_get_baseline` is the value; plumbing it needs
  `TextLayoutManagerExtended`.
- `numberOfLines` with `ellipsizeMode: 'clip'` does not truncate. Pango only
  honours a line limit when ellipsizing, so clip needs a clip node in the widget.
- Ignored: `adjustsFontSizeToFit`, `textBreakStrategy`, hyphenation,
  `textShadow*`, `textTransform`, `fontVariant`, `fontVariationSettings`.
- One PangoLayout is rebuilt per Paragraph per mutation, including
  layout-only updates that did not change the text.
- All measurement serialises on one mutex; see `plan/decisions.md`.
- Text is not selectable and reports nothing to AT-SPI.

## TextInput

- ~~**An uncontrolled field loses what was typed into it.**~~ Found on Windows
  in phase 46 and fixed on all three in phase 47. React Native's `TextInput.js`
  sends `text={value ?? defaultValue}`, so an uncontrolled field with no default
  sends `undefined` -- the empty string by the time it is C++, indistinguishable
  from a controlled field that was just cleared -- and it re-sends
  `mostRecentEventCount` on every change, which is an Update mutation on its
  own. Applying the prop whenever it differed from the widget therefore wiped
  the field on the user's second keystroke. It is now applied when the *prop*
  changes, because a controlled field's value changes as the user types and an
  uncontrolled one's never does, and a prop older than the last keystroke is
  dropped without being forgotten. Nobody had noticed because `js/input.js`
  asserts on its *controlled* field.
  - Still worth doing properly one day: iOS reads the shadow **state** rather
    than the prop, and writes the typed text into that state, so applying it
    back is a no-op and no heuristic is needed. That means a platform writing
    `TextInputState`, which none of these three do.
- No `multiline`. `RCTMultilineTextInputView` is not registered, and the C++
  side would need a `GtkTextView` peer rather than a `GtkText`.
- `onKeyPress` and `onSelectionChange` are never emitted. Both are cheap -- a
  `GtkEventControllerKey` and GtkText's `notify::cursor-position` -- and both
  were left out to keep the first version small.
- No `selection` prop, so a controlled selection is impossible.
- No shared focus registry: `TextInput.State.currentlyFocusedInput()` does not
  exist, and nothing else can ask what has focus. React Native's own
  `TextInputState` module talks to a TurboModule this platform does not have.
- `blur` grabs focus for the window rather than dropping it, because GTK models
  focus as moving, not as absent. The `onBlur` event is still correct.
- `placeholderTextColor`, `selectionColor` and `cursorColor` are parsed and
  ignored. GtkText takes those from CSS, not from a `PangoAttrList`, and this
  platform has no per-widget CSS provider.
- `src/overrides/TextInput.js` is a fork of React Native's component, and the only fork
  in the tree. Every prop upstream adds is a prop it will not have.
- `autoCapitalize`, `autoCorrect`, `spellCheck`, `keyboardType`,
  `returnKeyType`, `clearButtonMode`, `selectTextOnFocus` and
  `clearTextOnFocus` are ignored.

## Accessibility

- Not tested against a real screen reader. GTK's assertions say the properties
  are set; Orca on the Linux box is the check that matters.
- Accessible actions are unimplemented: `IMountingManager` declares
  `accessibleClickAction`, `setAccessibilityFocusedView`,
  `accessibleScrollInDirection` and `accessibleSetText`, and all are no-ops, so
  the interface can be read but not driven.
- `accessibilityRole` cannot change after mount; see `plan/decisions.md`.
- `accessibilityLiveRegion`, `accessibilityLabelledBy`, `accessibilityValue`
  and `accessibilityActions` are ignored.
- No keyboard focus model, so nothing is reachable by Tab.

## Components not implemented

`View`, `Text`, `Image`, `ScrollView` and `TextInput` are done, and touch input,
`PanResponder` and command routing with them. What is left:

- **`Modal`.** A second GTK window with its own Fabric surface, or an overlay
  inside the existing one. The first is more correct on a desktop and depends on
  multiple windows, above.
- **`Switch`** and **`ActivityIndicator`**, both small.
- **AT-SPI actions**, against the accessibility hooks `IMountingManager` already
  declares. See the accessibility section.

Notably *not* on this list, because a real application turned out to use none of
them: `FlatList`, `SectionList`, `Animated` with a native driver, `SafeAreaView`,
`KeyboardAvoidingView`. See `plan/10-first-real-app.md`.

## Ecosystem

- **Nobody else can use this yet** -- because nothing is published, and no
  longer because installing would not work. A fresh `create-expo-app` (SDK 57,
  React Native 0.86.3) with the four packages installed from their tarballs
  runs `react-native run-linux --build` and renders the template's text, with
  nothing from this repository on the path. `.github/workflows/release.yml`
  does exactly that on every release. Three things stood in the way, all in
  `--build`, and all found by writing that job:
  - ~~**It demanded a React Native source checkout.**~~ `buildHost` refused an
    installed `react-native`, long after bootstrap had learned to fetch the
    one directory the npm package lacks and CMake to build from the rest (see
    the `ReactCxxPlatform` entry under Upstream). Its error message told people
    to pass `--react-native-path`, which is not a flag.
  - ~~**It never wired in Expo.**~~ Nothing passed
    `-DBASALT_EXPO_MODULES_CORE`, worklets or Reanimated, so an Expo app got a
    host with no Expo runtime. It now passes whichever the app has installed.
  - ~~**It named no compiler.**~~ CMake took the system default, g++ on Ubuntu,
    and React Native's `-Werror` stopped it in ReactCommon. It names clang now,
    and on Windows clang-cl, a build type and vcpkg's toolchain file.

  On Windows the same app runs too, from `npm run windows -- --build` in a
  plain PowerShell: WSL's `bash.exe` first on PATH, no cmake, no vcvars.
  Getting there took `--build` finding Git Bash and loading the MSVC
  environment itself, and two fixes for expo-modules-core's C++, which had
  never been compiled by anything but clang and GCC: `JSI/ObjectDeallocator.h`
  says `#import`, which clang-cl reads as a COM type-library import, so on
  Windows the build compiles a copy with it spelled `#include`; and
  `TypedArray.cpp` throws `std::runtime_error` without `<stdexcept>`, which
  Microsoft's standard library does not pull in for it. Both are worth
  reporting to Expo.

  What is left before publishing is ordinary: versions, an npm account, the
  publish step, and an install guide. And a first `--build` that compiles Hermes
  and React Native's C++ from source, which is the part a user will notice.
- **Adding a desktop to an Expo app is manual.** Two dev dependencies --
  `@react-native/metro-config`, which React Native's `start` command requires
  whatever the app's Metro config says, and `@react-native-community/cli`,
  which is what provides `run-windows` -- plus `withDesktopPlatforms` in
  `metro.config.js` and a script per desktop. `npx react-native-basalt init`
  should do all of it, tested against a fresh `create-expo-app` as the release
  job's install is. Next up; see the README.
- **Porting a first third-party native module end to end**, to learn what the
  porting story actually costs. This is the largest unknown in the project: the
  TurboModule seam is proven, by `src/LinuxPlatformConstants.cpp`, but no
  third-party module has been through it, and there is no codegen configuration
  for one. Until a module has been ported, the cost of porting any module is a
  guess.
- Packaging: Arch PKGBUILD, Flatpak.

## Testing

- **Nothing tests tap-to-focus.** Clicking a `<TextInput>` focuses it on both
  hosts -- verified with a real `CGEvent` mouse click, after which a keystroke
  round-trips -- but no automated test can check that, and two obvious ways of
  trying give a false negative.

  `BASALT_TEST_TAP` enters at the touch dispatcher, below the window system, so
  it moves React Native's responder but never reaches the peer widget that
  actually takes focus. That is the same deliberate limitation `BASALT_TEST_TYPE`
  has with the key controller, and it looks exactly like a broken feature: the
  `<Pressable>` beside the field responds to an injected tap and the field does
  not. System Events' `click at` is no better -- it performs an accessibility
  press, which is why it answers with the name of the element it found, and a
  text field does nothing with one.

  So a tap-to-focus regression would be invisible: `integration_test.py`'s focus
  scenario taps a *button* that calls `focus()`, and every `<TextInput>` feature
  added in phases 51 and 52 was probed by focusing programmatically. Testing it
  needs a real click, which on macOS means `CGEventPost` and the accessibility
  permission that goes with it, and on Linux means xdotool -- which CI already
  has, and which is where this is worth adding.

- **A `<TextInput>`'s wrapper is still an element of its own on Windows.**
  Fixed on the other two in phase 53: the accessible name lands on the peer,
  which is what a screen reader reaches, and the wrapper leaves the tree --
  `accessibilityElement = NO` on AppKit, and `GTK_ACCESSIBLE_ROLE_PRESENTATION`
  chosen at construction on GTK, which is the only moment a GtkAccessible role
  can be chosen at all.

  Windows has not been looked at. UI Automation is the one that works
  differently -- a provider answers questions rather than a view carrying
  properties -- so the question there is whether the wrapper's provider should
  refuse to be a control, and whether the `EDIT` peer is exposed as its own
  element at all. `RnWin32Accessible.cpp` is where it would go.

  Whatever the answer, it must not print in `describeTree`: the other two say
  nothing about this view and a third vocabulary would put the cross-host diff
  back where phase 53 found it.

- **No unit test can observe an event.** Neither suite attaches an
  `EventEmitter` to the shadow views it builds, and `test_appkit_textinput.mm`
  says so in as many words: *"No emitter is attached to these hand-built shadow
  views, so what this pins is that the round trip did not throw."* That is a
  reasonable thing to pin and it is not coverage of the event.

  So nothing about `onChange`, `onFocus`, `onBlur`, `onKeyPress`,
  `onSelectionChange` or `onSubmitEditing` is tested anywhere below the
  end-to-end suite. Three of the four `<TextInput>` features added in phases 51
  and 52 rest on probe apps run by hand -- which is good evidence that they
  worked once, on the machine they were written on, and no evidence at all
  against a regression.

  A stub emitter handed back through `EmitterLookup` was the obvious fix and it
  does not work, which is worth recording so it is not tried twice.
  `TextInputEventEmitter` has no virtual methods at all -- the header contains
  the word `virtual` zero times -- so there is nothing to override, and
  constructing a *real* one reaches `EventDispatcher`, which needs an
  `EventBeat`, which needs a `RuntimeScheduler` and so a JavaScript runtime.
  That is the whole machinery the unit suites exist to avoid.

  What would work is a seam in the managers: route every emission through one
  private `emit(tag, kind, metrics)` that calls the emitter by default and can
  be pointed at a recorder in a test. Thirty-odd lines across the two managers,
  and it is production code carrying a test hook -- a trade worth making
  deliberately rather than by accident, which is why it is written here rather
  than done. `onKeyPress` firing before `onChange` is the case that most wants
  it, being an ordering guarantee nothing currently checks.

- **One flaky end-to-end scenario.** "focus a TextInput, type, and see it
  round-trip through React" failed once in five consecutive runs of
  `scripts/integration_test.py` with "the focus command did not move focus to
  the field", and passed the other four. The scenario schedules taps at fixed
  delays and assumes the host has caught up, which is a timing assumption rather
  than a synchronisation. Fixing it means waiting on something observable --
  the tree, or a log line -- instead of on a clock.

## Upstream

- **`http::Body::blob` is typed `std::optional<std::string>`** in
  ReactCxxPlatform, and `convertRequestBody` sends `{blobId, offset, size}` --
  an object. So a `Blob` request body throws "Value is an object, expected a
  String" in the bridging layer before reaching any platform's http client,
  identically on both desktops. The fix is a structured type or bridging that
  resolves the handle. See `plan/31-blobs.md`.
- **The cxx `NetworkingModule` does not mention blobs at all**, so
  `responseType: 'blob'` has no response path to hook and `addNetworkingHandler`
  has nothing to register with.

- **`ReactInstanceConfig` has no `platform` field.** `DevServerHelper` builds
  every bundle URL with `constexpr DEFAULT_PLATFORM = "android"`, so every
  desktop host asks Metro for an android bundle and the Metro plugin has to
  correct the request on arrival by reading the platform back out of `app=`.
  See `plan/21-js-platform-layer.md`. Worth a second upstream attempt now that
  two platforms need it rather than one.

- GTK 4.14's cairo renderer draws a transformed widget subtree unrotated and in
  the wrong colour; the GL renderer is correct. Worth reducing to a minimal case
  and reporting, or confirming it is already fixed in a later GTK.

- **React Native's npm package omits `ReactCxxPlatform`.** Worked around by
  fetching it at the app's exact version; see
  `plan/13-upstream-reactcxxplatform.md`. The upstream fix is one line and about
  1% of the package, and would remove the fetch entirely. Not raised: with no
  users to point at, the ask would sit. Worth revisiting when there are.
- The fetch needs a git tag matching the app's React Native. A nightly, a fork
  or an unreleased version has none, and there is no fallback.
- Also worth reporting, separately and smaller: the package ships
  `ReactCommon/react/nativemodule/cputime`'s C++ while its codegen spec lives
  under `src/private/testing/fantom` and does not ship, so that module cannot be
  compiled from the package. This platform stopped building it.
- Report the `HttpUtils.h` missing-`<cstdint>` bug. Since phase 41 there is a
  second of exactly the same shape and they should go together:
  `react/renderer/components/view/conversions.h` uses `M_PI` seven times, and
  `M_PI` is a POSIX extension rather than standard C++ -- MSVC's `<cmath>`
  defines it only behind `_USE_MATH_DEFINES`. Both compile on Meta's toolchains
  through luck rather than intent.
- **`ReactCommon/cmake-utils/react-native-flags.cmake` hardcodes clang's command
  line** -- `-Wall -Werror -fexceptions -frtti -std=c++20` -- and carries
  `TODO T228344694 improve this so that it works for all platforms` directly
  beneath. Worth attaching a concrete report to: MSVC's front end has none of
  those spellings, `-Wall` is actively misread by clang-cl as `/Wall` (which it
  maps to `-Weverything`), and because `-Wpedantic` is applied `INTERFACE` on
  `callinvoker` and `react_cxxstableapi` there is no flag a consumer can add
  that lands late enough to counteract it. Phase 41 works around it by stripping
  the flags from every target after `add_subdirectory`.
- Report that `ReactCxxPlatform`'s `PlatformConstantsModule` hardcodes a React
  Native version of 1000.0.0 in every version, releases included, so nothing
  built on it can ever satisfy React Native's own development-mode version
  check. Worked around in `src/LinuxPlatformConstants.cpp`.
- Consider upstreaming a Linux entry in `getHostPlatform.js` if the host build
  ever becomes something Meta would take.
