#include "WindowsModule.h"

#include "PlatformServices.h"
#include "WindowHost.h"

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

#include <string>
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
}

DesktopWindowsModule::~DesktopWindowsModule() {
  // The listener holds this module's emitter. Cleared on the way out, the same
  // arrangement the title bar has with its metrics listener.
  setHostWindowClosedListener(nullptr);
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
