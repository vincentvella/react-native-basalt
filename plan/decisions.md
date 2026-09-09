# Decisions

So they are not re-litigated.

## Path A over a Node-based reimplementation — 2026-09-08

**Rejected:** building on GTKX (`gtkx-org/gtkx`) / `react-native-gtkx`
(`itsmepetrov/react-native-gtkx`), which reimplement the RN API on Node with a
JS-side Yoga tree and no ReactCommon.

**Chosen:** a true out-of-tree platform embedding ReactCommon/Fabric/Hermes.

**Why:** the Node approach ships far sooner but yields a parallel ecosystem —
no existing RN native module works, ever. Path A costs much more up front and
gives a real porting path. Accepted cost: each native library still needs a
Linux backend written; "portable in principle" is not "works on day one".

## clang, not GCC — 2026-09-08

RN builds `-Wall -Werror -Wpedantic` and is a clang codebase. GCC fails on
`#pragma mark`, folly's `__int128` under `-Wpedantic`, and
`-Wsubobject-linkage` in `NetworkIOAgent`. Rather than paper over RN's own
warnings with `-Wno-*`, the project uses clang. Revisit only if GCC support
becomes a distribution requirement.

## Vendor folly/fast_float, use system glog/boost/fmt — 2026-09-08

folly is pinned to RN's exact version (`2024.11.18.00`) because RN builds a
trimmed subset of it and version skew is likely to hurt. glog and boost are
taken from the system despite RN pinning older versions, because the API
surface RN uses is stable — with one exception already hit, glog >= 0.6
requiring `GLOG_USE_GLOG_EXPORT`.

## RnLayout does no layout — 2026-09-08

Yoga resolves absolute frames before any mutation arrives, so `measure` returns
0 and `allocate` places children at their assigned rects. Letting GTK
participate in sizing would mean two layout systems disagreeing.
`react-native-gtkx` reached the same conclusion independently.

## The registry owns views between Remove and Delete — 2026-09-08

`g_object_ref_sink` on Create, `g_object_unref` on Delete. Fabric's Remove
detaches without destroying, and a view may be re-Inserted before its Delete
arrives, so parenting alone cannot own the lifetime.

## The first surface is driven by hand-written JS, not React — 2026-09-08

`ReactHost::startSurface` with an empty module name registers a shadow tree
without calling `AppRegistry.runApplication`: `SurfaceHandler::start` only
reaches into JS when a module name is set. That leaves a surface a plain script
can commit into through `nativeFabricUIManager`, the same JSI binding React's
Fabric renderer drives.

Taking that path meant phase 2 needed no `react` package, no Metro and no
bundler, so the milestone proves exactly one thing: the mutation stream now
comes from Fabric rather than from a harness. Bringing in React would have
mixed a bundler problem into a runtime problem.

The asymmetry to remember: `stopSurface` is *not* guarded the same way.
`UIManager::stopSurface` always calls `RN$stopSurface` in JS, so a script with
no React must install that global itself or every shutdown reports a fatal JS
error.

## The GTK4 replacement for size-allocate is the layout manager — 2026-09-08

`ReactHost::setSurfaceConstraints` has to be driven from the window's real
size, and the phase-2 plan assumed `GtkWidget::size-allocate`. GTK4 removed
that signal. A layout manager's `allocate` is the supported replacement and is
the one place a widget is told the size it actually got, so `RnLayout::allocate`
reports it through an optional callback on `RnView` and the host attaches one to
the surface root.

## http and websocket are host seams, and start out unimplemented — 2026-09-08

`getHttpClientFactory()` and `getWebSocketClientFactory()` are declared by
ReactCxxPlatform and defined nowhere in it — like
`getDefaultComponentRegistryFactory()`, every host supplies its own. Fantom
stubs both. `ReactHost` throws without them even when nothing makes a request,
so `src/LinuxNetworking.cpp` provides implementations that fail politely and
log. React Native already ships a working C++ websocket client at
`ReactCxxPlatform/react/http/platform/cxx/WebSocketClient.cpp` that its own
CMakeLists does not compile; wiring that up is phase 3 work, since the packager
connection is what needs it.
