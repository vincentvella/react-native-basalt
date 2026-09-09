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

## Platform surface

- `IImageLoader` via GdkPixbuf or glycin.
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
