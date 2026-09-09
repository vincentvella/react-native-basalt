# Backlog

Not scheduled. Roughly by value.

## Testing

- No rendering assertions: the widget tree says a view has a colour and a
  frame, not that the right pixels reached the screen. This is not theoretical
  -- GTK's cairo renderer mangled every transform in the demo and no test
  noticed. See `docs/TESTING.md`.
- Nothing exercises the JS thread and the main thread concurrently.
- The end-to-end scenarios hard-code tap coordinates from the demo's layout.
  Finding a button by its label in the dumped tree would survive a restyle.
- CI is Linux only. macOS is covered by whoever is developing, not by a machine.
- CI has no rendering assertions, so it cannot catch what the cairo renderer did.

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
  else. `ReactHost` takes a `logBoxSurfaceDelegate`; a second surface in its own
  GTK window is probably the cheapest real implementation.
- TurboModules React Native's JS asks for and does not get, none fatal today:
  `BlobModule`, `DeviceEventManager`, `SoundManager`, `LinkingManager`,
  `IntentAndroid`, `RedBox`, `ReactDevToolsSettingsManager`.
- `src/LinuxNetworking.cpp` supports only string request bodies. Blob, form-data
  and base64 need a Blob implementation first.
- The `linux` platform redirects nine React Native shims to their `.android.js`
  siblings. Each is a place this platform could diverge, and a place upstream
  could change under it; only `Platform` diverges today.
- Nothing checks that the shim list in `metro-config.js` still matches
  React Native. A new shim upstream shows up as an undefined export at runtime.

## Input

- No hover: W3C pointer events are not emitted, so `onMouseEnter` and friends
  never fire. They are a separate emitter path from touch, not a translation of
  it.
- `setIsJSResponder` is a no-op. It matters once something scrolls natively, so
  it lands with `ScrollView`.
- `Touch::offsetPoint` carries page coordinates rather than coordinates relative
  to the target view. Pressability does not read it; anything doing its own hit
  maths would.
- No keyboard, no focus, no key events.
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
- `require()`d assets are not resolved: the demo passes a path. Asset
  registration is bundler work and belongs with the npm package.
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

## Platform surface

- `TextInput`. No longer blocked on the platform package; needs a
  `TextInput.linux.js` naming a component this platform defines, plus keyboard
  input and a focus model.
- `Modal`, `Switch`, `ActivityIndicator` -- the remaining core components.
- Input: `GtkGestureClick` / `GtkEventControllerMotion` → RN's touch/pointer
  events; `setIsJSResponder` for the responder system.
- `dispatchCommand` routing (scrollTo, focus, blur) once ScrollView/TextInput
  exist.
- AT-SPI, against the accessibility hooks `IMountingManager` already declares.
- TurboModules: Linux implementations of core modules.

## Ecosystem

- `react-native-linux` npm package + `run-linux` CLI + codegen config.
- Packaging: Arch PKGBUILD, Flatpak.
- Porting a first third-party native module end to end, to learn what the
  porting story actually costs.

## Upstream

- GTK 4.14's cairo renderer draws a transformed widget subtree unrotated and in
  the wrong colour; the GL renderer is correct. Worth reducing to a minimal case
  and reporting, or confirming it is already fixed in a later GTK.

- Report the `HttpUtils.h` missing-`<cstdint>` bug.
- Consider upstreaming a Linux entry in `getHostPlatform.js` if the host build
  ever becomes something Meta would take.
