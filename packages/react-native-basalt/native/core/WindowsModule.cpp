#include "WindowsModule.h"

#include "PlatformServices.h"
#include "WindowControl.h"
#include "WindowHost.h"

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

#include <string>
#include <vector>
#include <utility>

namespace basalt {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::string stringProperty(Runtime &runtime, const Object &options, const char *name) {
  const Value value = options.getProperty(runtime, name);
  return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
}

double numberProperty(Runtime &runtime, const Object &options, const char *name, double fallback) {
  const Value value = options.getProperty(runtime, name);
  return value.isNumber() ? value.asNumber() : fallback;
}

} // namespace

DesktopWindowsModule::DesktopWindowsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["open"] = MethodMetadata{1, open};
  methodMap_["close"] = MethodMetadata{1, close};
  methodMap_["getWindows"] = MethodMetadata{0, getWindows};
  methodMap_["interceptClose"] = MethodMetadata{2, interceptClose};
  methodMap_["interceptQuit"] = MethodMetadata{1, interceptQuit};
  methodMap_["quit"] = MethodMetadata{0, quit};
  methodMap_["getDisplays"] = MethodMetadata{0, getDisplays};
  methodMap_["getPointerPosition"] = MethodMetadata{0, getPointerPosition};
  // What a NativeEventEmitter over this module calls; the event goes out as a
  // device event either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};

  // A window closed by the person rather than by the app. Without this the
  // `<Window>` that opened it would go on believing it is open; see
  // core/WindowHost.h.
  setHostWindowClosedListener([this](facebook::react::SurfaceId surfaceId) {
    emitDeviceEvent(kWindowClosedEvent,
                    [surfaceId](Runtime & /*runtime*/, std::vector<Value> &args) {
                      args.emplace_back(Value(static_cast<int>(surfaceId)));
                    });
  });

  // Somebody tried to close a window that asked to be asked first. The window
  // is still open; what happens next is the app's decision.
  setHostWindowCloseRequestListener([this](facebook::react::SurfaceId surfaceId) {
    emitDeviceEvent(kWindowCloseRequestedEvent,
                    [surfaceId](Runtime & /*runtime*/, std::vector<Value> &args) {
                      args.emplace_back(Value(static_cast<int>(surfaceId)));
                    });
  });

  // And somebody trying to quit an application that asked to be asked. No
  // argument: there is only one application.
  // A monitor plugged in, unplugged or rearranged. No payload: an app that
  // cares re-reads the list, which is the only way to be right when several
  // changes arrive together.
  setDisplaysListener([this]() {
    emitDeviceEvent(kDisplaysChangedEvent,
                    [](Runtime & /*runtime*/, std::vector<Value> & /*args*/) {});
  });

  setHostQuitRequestListener([this]() {
    emitDeviceEvent(kQuitRequestedEvent,
                    [](Runtime & /*runtime*/, std::vector<Value> & /*args*/) {});
  });
}

DesktopWindowsModule::~DesktopWindowsModule() {
  // The listener holds this module's emitter. Cleared on the way out, the same
  // arrangement the title bar has with its metrics listener.
  setHostWindowClosedListener(nullptr);
  setHostWindowCloseRequestListener(nullptr);
  setHostQuitRequestListener(nullptr);
  setDisplaysListener(nullptr);
}

Value DesktopWindowsModule::noop(Runtime & /*runtime*/,
                                 TurboModule & /*module*/,
                                 const Value * /*args*/,
                                 size_t /*count*/) {
  return Value::undefined();
}

Value DesktopWindowsModule::open(Runtime &runtime,
                                 TurboModule &module,
                                 const Value *args,
                                 size_t count) {
  NewWindowOptions options;
  if (count >= 1 && args[0].isObject()) {
    const Object given = args[0].asObject(runtime);
    options.component = stringProperty(runtime, given, "component");
    options.title = stringProperty(runtime, given, "title");
    options.width = numberProperty(runtime, given, "width", options.width);
    options.height = numberProperty(runtime, given, "height", options.height);

    const Value props = given.getProperty(runtime, "props");
    if (props.isObject()) {
      options.props = facebook::jsi::dynamicFromValue(runtime, props);
    }
  }

  auto promise = std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(
      runtime, static_cast<DesktopWindowsModule &>(module).jsInvoker_);

  // Onto the UI thread, where a window may be made, and back through the
  // promise. Blocking the JavaScript thread on a window manager is the thing
  // this shape exists to avoid.
  postToUiThread([promise, options = std::move(options)] {
    const facebook::react::SurfaceId opened = openHostWindow(options);
    // Null rather than zero for "could not": an id is a number, and a caller
    // checking `if (id)` would read a failure as window zero.
    promise->resolve(opened == 0 ? folly::dynamic(nullptr)
                                 : folly::dynamic(static_cast<int>(opened)));
  });

  return Value(runtime,
               facebook::react::bridging::toJs(
                   runtime, *promise, static_cast<DesktopWindowsModule &>(module).jsInvoker_));
}

Value DesktopWindowsModule::close(Runtime & /*runtime*/,
                                  TurboModule & /*module*/,
                                  const Value *args,
                                  size_t count) {
  if (count >= 1 && args[0].isNumber()) {
    const auto surfaceId = static_cast<facebook::react::SurfaceId>(args[0].asNumber());
    postToUiThread([surfaceId] { closeHostWindow(surfaceId); });
  }
  return Value::undefined();
}

Value DesktopWindowsModule::interceptClose(Runtime & /*runtime*/,
                                           TurboModule & /*module*/,
                                           const Value *args,
                                           size_t count) {
  if (count >= 1 && args[0].isNumber()) {
    const auto surfaceId = static_cast<facebook::react::SurfaceId>(args[0].asNumber());
    const bool intercepted = count >= 2 && args[1].isBool() && args[1].getBool();
    // No hop. This is read from inside a close handler on the UI thread, which
    // cannot wait for the JavaScript thread to get around to setting it -- and
    // it is a flag behind a mutex, not a toolkit call, so there is nothing to
    // marshal. Setting it late means one close that is not intercepted, which
    // is a window that closes; setting it on a hop could mean a close handler
    // reading a flag the app set several frames ago.
    setHostWindowCloseIntercepted(surfaceId, intercepted);
  }
  return Value::undefined();
}

namespace {

// One display, as JavaScript sees it. Flat rather than nested rectangles:
// `bounds` and `workArea` as objects would read better and would mean two
// more allocations per display per call, and this is read while an app is
// deciding where to put a window.
Object displayObject(Runtime &runtime, const DisplayInfo &info) {
  Object out(runtime);
  out.setProperty(runtime, "x", Value(info.x));
  out.setProperty(runtime, "y", Value(info.y));
  out.setProperty(runtime, "width", Value(info.width));
  out.setProperty(runtime, "height", Value(info.height));
  out.setProperty(runtime, "workX", Value(info.workX));
  out.setProperty(runtime, "workY", Value(info.workY));
  out.setProperty(runtime, "workWidth", Value(info.workWidth));
  out.setProperty(runtime, "workHeight", Value(info.workHeight));
  out.setProperty(runtime, "scaleFactor", Value(info.scaleFactor));
  out.setProperty(runtime, "primary", Value(info.primary));
  return out;
}

} // namespace

Value DesktopWindowsModule::getDisplays(Runtime &runtime,
                                        TurboModule & /*module*/,
                                        const Value * /*args*/,
                                        size_t /*count*/) {
  // Off the cache rather than the toolkit, so this needs no hop and can be
  // read during render -- the same arrangement getBounds has. The host
  // refreshes it at startup and on every change, so it is exact.
  const std::vector<DisplayInfo> found = lastKnownDisplays();

  Array result(runtime, found.size());
  for (size_t i = 0; i < found.size(); i++) {
    result.setValueAtIndex(runtime, i, displayObject(runtime, found[i]));
  }
  return result;
}

Value DesktopWindowsModule::getPointerPosition(Runtime &runtime,
                                               TurboModule &module,
                                               const Value * /*args*/,
                                               size_t /*count*/) {
  auto promise = std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(
      runtime, static_cast<DesktopWindowsModule &>(module).jsInvoker_);

  // A promise rather than a cached read, because there is nothing to cache:
  // the answer changes whenever the pointer moves and no host reports that.
  // So this is the one display question that has to go and ask.
  postToUiThread([promise] {
    const PointerPosition where = pointerPosition();
    folly::dynamic out = folly::dynamic::object;
    out["x"] = where.x;
    out["y"] = where.y;
    // So that an app can tell "at the origin" from "this desktop will not
    // say" -- which GTK never will; see GtkWindowControl.cpp.
    out["known"] = where.known;
    promise->resolve(std::move(out));
  });

  return Value(runtime,
               facebook::react::bridging::toJs(
                   runtime, *promise, static_cast<DesktopWindowsModule &>(module).jsInvoker_));
}

Value DesktopWindowsModule::quit(Runtime & /*runtime*/,
                                 TurboModule & /*module*/,
                                 const Value * /*args*/,
                                 size_t /*count*/) {
  // Hopped, unlike interceptQuit: this one ends the process through a
  // toolkit call, and those belong on the thread that owns the toolkit.
  postToUiThread([] { quitHost(); });
  return Value::undefined();
}

Value DesktopWindowsModule::interceptQuit(Runtime & /*runtime*/,
                                          TurboModule & /*module*/,
                                          const Value *args,
                                          size_t count) {
  // No hop, for the reason interceptClose gives: this is read on the UI
  // thread from inside a terminate handler that cannot wait for the
  // JavaScript thread, and it is a flag behind a mutex rather than a toolkit
  // call.
  setHostQuitIntercepted(count >= 1 && args[0].isBool() && args[0].getBool());
  return Value::undefined();
}

Value DesktopWindowsModule::getWindows(Runtime &runtime,
                                       TurboModule & /*module*/,
                                       const Value * /*args*/,
                                       size_t /*count*/) {
  // Read rather than hopped: this is bookkeeping the host keeps in a vector,
  // and an app asking during render cannot wait for a thread.
  const std::vector<facebook::react::SurfaceId> open = hostWindows();
  Array result(runtime, open.size());
  for (size_t i = 0; i < open.size(); i++) {
    result.setValueAtIndex(runtime, i, Value(static_cast<int>(open[i])));
  }
  return Value(runtime, result);
}

} // namespace basalt
