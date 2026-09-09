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

## Bundles are built for the `android` platform — 2026-09-09

`ReactCxxPlatform`'s `PlatformConstantsModule` returns
`PlatformConstantsAndroid`, and `DevServerHelper` hardcodes
`DEFAULT_PLATFORM = "android"` in the bundle URL it requests. React Native's JS
therefore already believes it is on Android when hosted through this platform,
and Metro has to resolve the matching `.android.js` files.

Bundling with `--platform linux` is not a small change of flag. React Native's
own internals have no `.linux.js` variants, starting with `Platform`, so
resolution fails outright. A real `linux` platform means shipping a JS package
that supplies its own `Platform` module and native component registry, as
`react-native-windows` and `react-native-macos` do. Until then, adding `linux`
to `resolver.platforms` would only move the failure somewhere less obvious.

## The demo app has no node_modules — 2026-09-09

`js/` is not an installed npm package. `metro.config.js` points `watchFolders`
and `resolver.nodeModulesPaths` at the React Native checkout that
`scripts/bootstrap.sh` already prepared, so `react`, `react-native`, Metro and
the Babel preset all resolve from there. Nothing is installed twice, and the
demo cannot drift from the source tree the C++ side is compiled against.

Two sharp edges. `extraNodeModules` pins `react` to one copy, because two React
copies in a graph produce the usual invalid-hook-call at runtime. And the
workspace packages must be required as package *specifiers*, not absolute
paths -- their entry points live only in an `exports` field, which Node ignores
when you require a directory by path.

## http is libcurl; the websocket is React Native's own — 2026-09-09

`getHttpClientFactory()` is implemented in `src/LinuxNetworking.cpp` over
libcurl, one thread per request. Every exit path ends in `onResponseComplete`,
because `DevServerHelper::downloadBundleResourceSync` blocks on a `std::future`
that only the callbacks complete: a client that quietly drops them does not
fail there, it hangs.

`getWebSocketClientFactory()` is *not* written here. React Native already ships
a working boost::beast client at
`ReactCxxPlatform/react/http/platform/cxx/WebSocketClient.cpp` which its own
CMakeLists never compiles, because the `react/http` glob covers only that one
directory. This project builds it as `rn_websocket`. The cost is two extra
dependencies: folly's `Uri.cpp`, which React Native does not build either, and
`boost_regex`, which `Uri.cpp` needs.

## Fast Refresh arrives as a reload, not a patch — 2026-09-09

Editing a module and seeing the window update goes through two channels.
React Native's JS HMR client connects to Metro over `WebSocketModule`, and
`ReactHost` separately opens a packager connection whose reload message calls
`reloadReactInstance()`. For an edit to a module with no refresh boundary --
`js/index.js` registers the app, so it has none -- the observed path is
`DevSettingsModule::reloadWithReason: Fast Refresh - No root boundary`, i.e. a
full instance reload. That is the same behaviour as iOS and Android for that
kind of edit, not a limitation of this platform.
