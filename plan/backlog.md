# Backlog

Not scheduled. Roughly by value.

## macOS

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

- No rendering assertions: the widget tree says a view has a colour and a
  frame, not that the right pixels reached the screen. This is not theoretical
  -- GTK's cairo renderer mangled every transform in the demo and no test
  noticed. See `docs/TESTING.md`.
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
- **`Clipboard`** (throws on import). GTK has `GdkClipboard`; the work is the
  module, not the mechanism.
- **`Vibration`** (throws on import). Meaningless on a desktop, but it has to
  answer rather than throw, so a no-op module is enough.
- **`LinkingManager`** (soft). Opening a URL, and receiving one. `gio`'s
  `g_app_info_launch_default_for_uri` covers the outbound half.
- **`AlertManager`** / **`DialogManagerAndroid`** (soft). `Alert.alert()` does
  nothing at all today, which is a quiet way to lose a confirmation dialog.
- **`I18nManager`** (soft). Right-to-left layout. Yoga already supports it; this
  is the switch that turns it on.
- **`AccessibilityInfo`** (soft). Whether a screen reader is running, and
  announcements. Pairs with the AT-SPI work below.
- **`ShareModule`** (soft).
- `BlobModule` and `FileReaderModule`, without which `fetch` cannot return a
  blob and `src/LinuxNetworking.cpp` stays limited to string bodies.

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
- Report the `HttpUtils.h` missing-`<cstdint>` bug.
- Report that `ReactCxxPlatform`'s `PlatformConstantsModule` hardcodes a React
  Native version of 1000.0.0 in every version, releases included, so nothing
  built on it can ever satisfy React Native's own development-mode version
  check. Worked around in `src/LinuxPlatformConstants.cpp`.
- Consider upstreaming a Linux entry in `getHostPlatform.js` if the host build
  ever becomes something Meta would take.
