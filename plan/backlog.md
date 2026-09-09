# Backlog

Not scheduled. Roughly by value.

## Correctness gaps in what exists

- `borderRadii` / `borderWidth` / `borderColor` — needs
  `gtk_snapshot_push_rounded_clip` plus a border node.
- `transform` — `gsk_transform_*`, including transform-origin.
- `overflow: hidden` — currently everything is `visible` (visible in the phase-1
  screenshots, where a child outgrows its shrunk parent).
- `displayType == DisplayType::None` should hide the widget.
- `pointScaleFactor` — fractional scaling under Wayland.
- `zIndex` — needs an explicit paint order in `RnView::snapshot`; GTK paints in
  child order today. (react-native-gtkx has measurements on the cost of doing
  this without a GObject vfunc chain-up.)

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
- A real `linux` Metro platform, which means a JS package with its own
  `Platform` module; see `plan/decisions.md`.

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
- Real GDK event delivery is unverified in automation. `RN_LINUX_TEST_TAP`
  enters at the gesture callback, so everything downstream is covered and GDK's
  routing is not. Verifying it needs a machine where synthesising a pointer
  event is allowed.

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

## Platform surface

- `IImageLoader` via GdkPixbuf or glycin, for `<Image>`.
- `ScrollView`, on a `GtkScrolledWindow` peer.
- `TextInput`, which needs keyboard input and a focus model first.
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

- Report the `HttpUtils.h` missing-`<cstdint>` bug.
- Consider upstreaming a Linux entry in `getHostPlatform.js` if the host build
  ever becomes something Meta would take.
