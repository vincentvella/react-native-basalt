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

## Text: replace React Native's stub, do not add a platform variant — 2026-09-09

`TextLayoutManager` has a header in React Native's cxx platform variant and one
implementation there, a stub that ignores every attribute and returns
`layoutConstraints.minimumSize`. The header is already generic, so this project
keeps it and supplies only the .cpp: `src/PangoTextLayoutManager.cpp` defines the
same symbols against Pango, and `cmake/ReactNativeCore.cmake` removes the stub
source from `react_renderer_textlayoutmanager` after `add_subdirectory` so the
two do not collide.

The alternative was a full `platform/linux` tree with a duplicate header. That
buys nothing while the interface is unchanged, and it would have to be kept in
sync with upstream by hand.

## One layout builder for measuring and painting — 2026-09-09

`src/PangoTextLayout.cpp` is used by both `TextLayoutManager::measure` and
`GtkMountingManager`. This is not tidiness: if the two built layouts differently
-- a different default font, a different wrap mode -- Yoga would allot a box
computed one way and the widget would paint text laid out another way, and the
result is clipped or overlapping text that looks like a rendering bug rather
than a measurement one. Sharing the builder makes that class of bug impossible.

Pango's font map is not documented as reentrant and this is reached from Fabric's
layout thread and the GTK main thread, so a single mutex covers every use. That
serialises all text measurement, which the `textMeasureCache_` mostly hides.

## Font sizes are absolute, not points — 2026-09-09

`pango_font_description_set_size` takes points and resolves them against the
context's resolution, so at the default 96dpi a `fontSize` of 16 renders at about
21px. React Native's `fontSize` is in density-independent pixels, and every
coordinate on this platform -- Yoga's frames, the widget's allocation -- lives in
that same logical space. So sizes go through `set_absolute_size`, and nothing
here multiplies by `pointScaleFactor`: GTK applies the display scale when it
renders the widget tree.

## Ellipsization needs a line limit, or it eats the paragraph — 2026-09-09

React Native's `ellipsizeMode` defaults to `Tail`, and setting
`pango_layout_set_ellipsize(END)` without also setting a height does not mean
"ellipsize on overflow": with no height, Pango ellipsizes to a *single line*.
Translating the default faithfully therefore collapsed every wrapping paragraph
to one line. Ellipsization is only applied when `maximumNumberOfLines` is set,
which is also the only case where React Native means it.

## The event beat is not optional, and it is a GSource — 2026-09-09

`EventQueue::onEnqueue` only sets a flag on the `EventBeat`. Nothing an
`EventEmitter` produces reaches JavaScript until something calls
`RunLoopObserverManager::onRender()`. Phases 2 to 4 never noticed, because mounts
arrive through `executeMount` rather than the event queue; input is the first
thing that depends on it, and without it touches are enqueued and silently
dropped.

React Native asks for `Activity::BeforeWaiting`: run once the loop has drained
its work and is about to sleep. The GLib equivalent is a `GSource` that does the
work in `prepare()` and never reports itself ready -- `prepare()` runs once per
main-loop iteration before the poll, so it costs one call when the loop is busy
and nothing when the app is idle.

A `gtk_widget_add_tick_callback` on the root would also work and was the obvious
first idea, but it holds the frame clock open and wakes the process at display
rate for as long as the window is mapped. That is the wrong trade for a desktop
app that spends most of its life still.

## Input is touch events, and one set of controllers on the root — 2026-09-09

React Native's Pressability -- what backs every `onPress` -- runs on the
responder system in JavaScript, and the responder system is fed by
touchstart/touchmove/touchend. W3C pointer events exist alongside them but are
consulted only for hover, behind `shouldPressibilityUseW3CPointerEventsForHover`.
So a desktop pointer is reported as a single touch point, which is also what
React Native for Windows and macOS do.

The controllers live on the surface root rather than on each view. Per-widget
controllers would have to be created and destroyed on every mutation, and would
still need a hit test; `gtk_widget_pick` already walks the tree and returns the
deepest widget at a point, which is the answer React Native's hit testing wants
now that every view is allocated at the frame Yoga assigned it.

Two details that are easy to get wrong. A gesture reports against the view it
*started* on for its whole life, even after the pointer leaves, because that is
what the responder system expects. And on touchend the touch must not appear in
`touches`, only in `changedTouches` -- leaving it in convinces the responder
system a finger is still down and it swallows the next press.

## <Image> loads its own pixels — 2026-09-09

React Native's cxx `ImageManager` is a stub: `requestImage` returns
`ImageRequest{source, nullptr, {}}`, so no `ImageResponse` ever arrives and
`ImageState` never carries anything to render. The platform view is expected to
load its own image, which is what Android does too -- Fresco, from
`ReactImageView`, not from the shadow node.

So `GtkImageLoader` reads the URI off `ImageProps::sources` and produces a
`GdkTexture`. I/O runs on a worker thread; the decode happens back on the main
thread, because `GdkTexture` is a GObject and the expensive part is the read.

Two lifetime rules fall out. The completion looks the view up by tag rather than
capturing the widget, because a view can be deleted while its image is in
flight. And a mutation that changed only layout must not restart the load, or an
`<Image>` flickers whenever its parent resizes -- so the current URI is tracked
per tag and an unchanged one is served from cache.

## <ScrollView> is an offset, not a GtkScrolledWindow — 2026-09-09

`GtkScrolledWindow` sizes its child through the measure/allocate protocol, and
this platform's whole invariant is that React Native decides sizes and
`RnLayout` only places things. Using it would mean teaching `RnLayout` to report
a real size, i.e. two layout systems disagreeing.

Instead a ScrollView is an `RnView` that clips and carries a scroll offset, and
`RnLayout::allocate` subtracts that offset from each child's frame. Yoga has
already laid the content out at full size -- the ScrollView's node carries
`overflow: scroll`, which is what lets its child exceed the viewport -- so the
offset is the only thing missing. Placing children at their scrolled positions
also means `gtk_widget_pick` follows the scroll, so hit testing needs no
special case.

The child structure matters and is easy to get wrong: a mounted ScrollView has
**one** child, not N. React Native's JS wraps the children in an
`RCTScrollContentView`, which `componentNameByReactViewName` rewrites to a plain
`View`, so no extra descriptor is needed.

## Two things happen on every scroll, and only one is throttled — 2026-09-09

`onScroll` goes to JavaScript and is throttled by `scrollEventThrottle`, which
is the platform's job on every platform. Without it VirtualizedList never
renders past its first window.

Writing `contentOffset` back into `ScrollViewState` is separate and must not be
throttled. `ScrollViewShadowNode::getContentOriginOffset` reads it, and through
that so do `measure`, `measureLayout`, C++ hit testing (`findNodeAtPoint`) and
view culling. Throttling it leaves all of those quietly reporting an unscrolled
position.

One more trap: `ScrollEvent::zoomScale` defaults to **0**, not 1, and
VirtualizedList only repairs negative values. Leaving the default makes every
list measurement come out as zero.

## Accessible roles are chosen at construction — 2026-09-09

GTK4 has no per-instance setter for an accessible role: it is a construct-only
property, or is set once per widget class. `RnView` is one class for every React
Native view, so the role has to be decided when the widget is made. That works
because Fabric delivers a view's props with the Create mutation that makes it,
but it does mean `accessibilityRole` cannot change after mount. Everything else
-- label, hint, states -- updates freely.

An unrecognised role falls back to `GENERIC` rather than a guess. A wrong role
is worse than none: it makes a widget announce itself as something it is not.
Where the app says nothing, the component decides -- a `<Text>` is a label and
an `<Image>` an image whether or not anyone asked.

States are tri-state on purpose. Leaving `checked` unset is not the same as
setting it false: a view that never mentions being checked is not an unchecked
checkbox, and a screen reader should not read it as one.

## <TextInput> is blocked on shipping our own JS component — 2026-09-09

Both of React Native's built-in text inputs are unusable here, for different
reasons.

Bundles are built for the `android` platform, so JavaScript asks for
`AndroidTextInput`. Its descriptor includes `<fbjni/fbjni.h>` and reaches into
a Java `FabricUIManager` for theme padding, so it cannot be compiled off
Android at all.

The iOS descriptor, `TextInputComponentDescriptor`, *is* portable -- it needs
only a `TextLayoutManager`, which this platform now has. But its component name
is `TextInput`, and JavaScript only asks for that name when the bundle is built
for iOS: `componentNameByReactViewName` maps `SinglelineTextInputView` and
`MultilineTextInputView` onto it. Bundling as iOS to reach it would contradict
`PlatformConstantsModule`, which reports Android, and `DevServerHelper`, which
hardcodes `platform=android` into the bundle URL.

So `<TextInput>` needs a JavaScript component of our own, mapping to a
component name this platform defines -- which is the same blocker as a real
`linux` Metro platform. It is a packaging problem wearing a rendering problem's
clothes, and doing it by halves would mean either an unbuildable descriptor or
a bundle that lies about what platform it is on.

## transform is composed during layout, not at paint time — 2026-09-09

`gtk_snapshot_transform` would have been the obvious place, and it would have
been wrong: a transform applied while painting moves the pixels but not the
widget, so `gtk_widget_pick` still finds the view at its untransformed frame and
a rotated button is clickable where it *used* to be.

Composing it into the `GskTransform` that `RnLayout::allocate` hands to
`gtk_widget_allocate` moves the widget itself, and GTK's own picking follows --
the same reason the scroll offset lives there.

The anchor is the view's centre. `resolveTransform` folds in `transformOrigin`
only when one is set, and the offsets it produces are measured *from the
centre*, so the platform has to supply that anchor. Every other React Native
platform does the same, iOS through its layer's default anchor point.

React Native's matrix is CSS `matrix3d` order and graphene's is nominally
row-major. The two coincide in memory for translation and scale but are
transposes for rotation, which no unit test on an axis-aligned box can tell
apart. The demo therefore rotates a card with a marker in one corner: a positive
angle turns clockwise, so the marker must end up on the other side. It does, so
no transpose is needed.

## zIndex reorders painting, never the child list — 2026-09-09

Fabric's Insert and Remove mutations carry an `index` into the parent's child
list, so that list has to stay in mutation order. `RnView::snapshot` therefore
sorts a *copy* by zIndex when any child has one, and skips the sort entirely
when none does, which is the common case. The sort is stable, so equal zIndex
keeps document order -- what CSS and React Native both promise.

## The `linux` platform is nine redirects and one real file — 2026-09-09

Bundling for a platform React Native has never heard of fails in three
different ways, and only the third is the interesting one.

Nine files are self-importing shims: their whole body is
`import X from './X'; export default X;`, marked "backwards compatibility of
subpath (deep) imports". They exist so `react-native/Libraries/Image/Image`
resolves, and they assume a platform-specific sibling will win. On `linux` each
resolves to itself and exports undefined, and they fail one at a time, far from
the cause -- `Platform.constants` undefined, then a view config undefined, then
a component undefined. Finding them by crashing takes an afternoon; finding them
with one grep for that note takes a minute, which is why the list is spelled out
in `metro-config.js` rather than discovered.

Each is answered with React Native's own `.android.js` sibling rather than a
copy. This platform reports `PlatformConstantsAndroid` from C++, shares
ReactCommon's prop parsing, and drives the components Android's JavaScript
drives, so Android's implementation is the one that matches what is actually
here. Nine forks would drift from upstream in silence.

A second kind does not resolve at all -- `ReactDevToolsSettingsManager` ships
only as `.android.js` and `.ios.js` -- so there is no resolution to rewrite,
only a failure to catch. That one is a real file, a no-op, because the
TurboModule behind it does not exist here either.

`Platform` is the only module this platform genuinely implements: `OS: 'linux'`,
and a `select` that prefers `linux`.

And one thing the C++ side simply cannot be told: `DevServerHelper` builds its
bundle URL from `constexpr DEFAULT_PLATFORM = "android"`, with no hook. Left
alone, an app would be `Platform.OS === 'android'` under Fast Refresh and
`'linux'` in a release build -- a worse trap than either value on its own. Metro's
`server.rewriteRequestUrl` corrects the request on arrival instead.

## `<TextInput>` reuses iOS's C++ and forks its JavaScript — 2026-09-09

Both halves of that were forced, in opposite directions.

The C++ was free. React Native's Android text input includes `fbjni` and calls
a Java `FabricUIManager`; its iOS one includes nothing but ReactCommon and
measures through whatever `TextLayoutManager` is installed, which here is the
Pango one. So `TextInputComponentDescriptor`, `TextInputProps`,
`TextInputShadowNode`, `TextInputState` and `TextInputEventEmitter` are compiled
unchanged, under the name they declare, `TextInput`. React Native's own
`componentNameByReactViewName` already maps `RCTSinglelineTextInputView` to it.

The JavaScript could not be. `TextInput.js` is
`if (Platform.OS === 'android') { ... } else if (Platform.OS === 'ios') { ... }`
with no third branch, so on `linux` every component and command binding stays
undefined and React reports an invalid element type. The alternatives were a
third branch upstream, a patch, or a smaller file of our own. The first is not
ours to make; the second drifts silently; the third is honest about being a
subset. It is the only fork in the tree and should stay that way -- it is a
debt, not a pattern.

The editing itself is a real `GtkText` rather than a caret drawn on a
`PangoLayout`. Input methods, selection, the clipboard and every Linux
keybinding come with it, and each is easy to get subtly wrong. The cost is that
the widget holds state React Native believes it owns, which is what the
`applying` flag, the preserved cursor position and the `eventCount` check exist
to reconcile.

## Imperative commands run on the main thread — 2026-09-09

`schedulerDidDispatchCommand` arrives on the JavaScript thread, inside the event
loop's rendering update, exactly as `executeMount` does. `dispatchCommand` was
calling straight into GTK from there, which was survivable only because the only
commands were `ScrollView`'s: moving an adjustment touches nothing reentrant.
`gtk_text_grab_focus` reaches the platform input method, and on macOS AppKit
asserts it is on the main thread and traps the process.

Commands now queue through `g_idle_add_full` at the same priority as a mount.
That is not just for safety: equal-priority idle sources run in the order they
were added, so a command still lands behind the transaction that created the
view it names, which is the ordering React Native's `focus()`-on-mount depends
on.
