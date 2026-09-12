# Backlog

Not scheduled. Roughly by value.

## Windows

Since phase 47 Windows is a peer: it mounts every component the other two
desktops do, it runs the same end-to-end suite, it has a `run-windows`, and
`compare_hosts.sh` knows how to run it. What is left below is real and named,
and none of it is a missing half.

Since phase 46 Windows mounts every component the other two desktops do:
`<View>`, `<Text>`, `<Image>`, `<ScrollView>` and `<TextInput>`. Hermes
evaluates a bundle, Fabric diffs a shadow tree, Direct2D paints it in an HWND, a
click on a `<Pressable>` runs its `onPress`, a wheel scrolls a list, and a
character typed into a field reaches React and comes back. Struck-through
entries below are what that took, kept because the order they were done in is
the useful part.

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
- **Nothing has run bootstrap from an empty `third_party`** on Windows. Every
  step is exercised, but each one skipped its work because the output was
  already there from doing it by hand -- so what is proven is that the checks
  pass and the printed configure line works, not that the fetching and building
  do. The Hermes patch was tested separately against a reverted copy of
  `Callable.h`, applies cleanly and is a no-op on a second pass; that is the
  part most worth proving properly, since `--force` deletes the tree it patches.
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
  has and a finger does not: **no hover**, so `onMouseEnter` and `:hover`-style
  props never fire; **no right button**; **no keyboard**, which arrives with
  `<TextInput>` because there is nothing yet that focus could belong to. The
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
- **No machine has run `scripts/compare_hosts.sh` with Windows in it.** The
  script knows about three hosts as of phase 47 and runs whichever are built,
  and the Windows leg was exercised on its own: it produces a tree, reports
  `Platform.OS is windows`, and prints `editable="..."` where GTK does. What has
  never happened is two hosts on one machine -- WSL2 will not start here because
  virtualisation is disabled in firmware. A Mac with GTK installed could do it
  today, and only that turns "the dumps are written to match" into "the dumps
  match".
- **`scripts/integration_test.py` skips Fast Refresh on Windows**, because
  `scripts/metro.sh` is a shell script. Everything it would prove about the
  *host* is the same code on every platform; what would be platform-specific is
  Metro's file watching, which is Metro's. The other four scenarios run.
- **CI does not build the Windows host.** Its job is the view layer only --
  two minutes, no React Native, no Hermes -- so 63 of the 149 Windows tests run
  there and the mounting manager, the input path, `<ScrollView>` and
  `<TextInput>` are covered by a developer's machine alone. A full job means
  vcpkg, a React Native checkout and a Hermes build on a Windows runner: an
  hour cold, and worth doing once the caching is understood well enough not to
  pay it every run.

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
- CI is Linux only. macOS is covered by whoever is developing, not by a machine.
- **The Fast Refresh scenario is skipped in CI**, so nothing on a machine
  protects development mode. Metro on a GitHub runner never notices an edit:
  the file changes on disk with a fresh mtime, a newly requested bundle still
  carries the old text, and Metro logs nothing. Ruled out already: `fs.watch`
  sees the same edit on the same runner; the inotify limits are 655360 watches
  and 1280 instances; both sides run Node 24.20.0; neither has watchman, so
  both use the same node watcher; and CI's layout, with React Native inside the
  checkout, reproduces green in a local VM. The scenario passes on macOS and on
  Linux in a VM, so this is about the runner rather than the code.

  Next thing to try, in order of effort:

  1. **Install watchman in CI** and see whether the scenario passes. Both sides
     currently fall back to metro-file-map's own node watcher, and that is the
     component under suspicion, so putting watchman in front of it on the
     runner both tests the theory and would be the fix if it works. Add it to
     the dependencies step, then drop `BASALT_SKIP_FAST_REFRESH` from the
     end-to-end step and read the result.

     Two things to know before starting. Ubuntu noble packages watchman, but
     at 4.9.0, which is from 2017; whether metro-file-map's client is happy
     with something that old is the first thing to find out, and a failure
     there would say nothing about the theory. Homebrew's is current, so a Mac
     and an apt-installed Linux box would not be running the same watchman
     either. And it would leave CI and development machines on different
     watchers, with development on the untested one -- so if watchman does fix
     CI, installing it locally too is the honest follow-up rather than
     declaring the problem solved.
  2. `DEBUG=metro:*` on the CI Metro, to see what the watcher thinks it is
     doing rather than inferring it from what the bundle contains.
  3. Shrink `watchFolders`. The React Native checkout is the bulk of the eight
     thousand directories; if the node watcher is falling over on volume, a
     narrower watch would show it.
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
- **Window title, size, position, fullscreen and close behaviour**, none of
  which an app can currently influence.
- **Menus**, both a menu bar and context menus.
- **Native file dialogs.** `GtkFileDialog` exists; nothing exposes it. Note that
  kino's own macOS module ships a folder picker, so this is what a real app
  reaches for early.
- **Drag and drop**, in and out of the application.
- **A system tray icon**, and desktop notifications.
- **Cursor control** beyond what the `cursor` style property covers.

## Input

- No hover: W3C pointer events are not emitted, so `onMouseEnter` and friends
  never fire. They are a separate emitter path from touch, not a translation of
  it.
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

- **Nobody else can use this.** No npm package, no `run-linux` command, no
  install path. Everything runs from scripts in this repo against a host you
  build yourself, which is the difference between a working platform and a
  usable one.
- **Porting a first third-party native module end to end**, to learn what the
  porting story actually costs. This is the largest unknown in the project: the
  TurboModule seam is proven, by `src/LinuxPlatformConstants.cpp`, but no
  third-party module has been through it, and there is no codegen configuration
  for one. Until a module has been ported, the cost of porting any module is a
  guess.
- Packaging: Arch PKGBUILD, Flatpak.

## Testing

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
