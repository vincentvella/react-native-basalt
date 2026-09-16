#import "AppKitWindowModule.h"

#import "AppKitTitleBar.h"
#include "PlatformServices.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;
using facebook::react::TurboModule;

constexpr const char *kMetricsChangedEvent = "basaltTitleBarMetricsChanged";

// processColor's 0xAARRGGBB, or nullopt for anything that is not a number --
// which is how JavaScript asks for the system's colour back.
std::optional<uint32_t> colorArgument(const Value *args, size_t count, size_t index) {
  if (index >= count || !args[index].isNumber()) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(static_cast<int64_t>(args[index].asNumber()));
}

Value metricsValue(Runtime &runtime, const TitleBarMetrics &metrics) {
  Object object(runtime);
  object.setProperty(runtime, "height", metrics.height);
  object.setProperty(runtime, "buttonsWidth", metrics.buttonsWidth);
  object.setProperty(runtime,
                     "style",
                     String::createFromAscii(
                         runtime, metrics.style == TitleBarStyle::Hidden ? "hidden" : "native"));
  return Value(runtime, object);
}

} // namespace

AppKitWindowModule::AppKitWindowModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["setTitle"] = MethodMetadata{1, setTitle};
  methodMap_["setTitleBarColors"] = MethodMetadata{3, setTitleBarColors};
  methodMap_["setTitleBarStyle"] = MethodMetadata{1, setTitleBarStyle};
  methodMap_["getTitleBarMetrics"] = MethodMetadata{0, getTitleBarMetrics};
  // What a NativeEventEmitter over this module calls; the events go out as
  // device events either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};

  titleBar().setMetricsListener([this](const TitleBarMetrics &metrics) {
    emitDeviceEvent(kMetricsChangedEvent, [metrics](Runtime &runtime, std::vector<Value> &args) {
      args.emplace_back(metricsValue(runtime, metrics));
    });
  });
}

AppKitWindowModule::~AppKitWindowModule() {
  titleBar().setMetricsListener(nullptr);
}

Value AppKitWindowModule::setTitle(Runtime &runtime,
                                   TurboModule & /*module*/,
                                   const Value *args,
                                   size_t count) {
  std::optional<std::string> title;
  if (count > 0 && args[0].isString()) {
    title = args[0].getString(runtime).utf8(runtime);
  }
  postToUiThread([title = std::move(title)]() mutable { titleBar().setTitle(std::move(title)); });
  return Value::undefined();
}

Value AppKitWindowModule::setTitleBarColors(Runtime & /*runtime*/,
                                            TurboModule & /*module*/,
                                            const Value *args,
                                            size_t count) {
  const auto background = colorArgument(args, count, 0);
  const auto text = colorArgument(args, count, 1);
  const auto border = colorArgument(args, count, 2);
  postToUiThread([background, text, border] { titleBar().setColors(background, text, border); });
  return Value::undefined();
}

Value AppKitWindowModule::setTitleBarStyle(Runtime &runtime,
                                           TurboModule & /*module*/,
                                           const Value *args,
                                           size_t count) {
  const bool hidden =
      count > 0 && args[0].isString() && args[0].getString(runtime).utf8(runtime) == "hidden";
  postToUiThread([hidden] {
    titleBar().setStyle(hidden ? TitleBarStyle::Hidden : TitleBarStyle::Native);
  });
  return Value::undefined();
}

Value AppKitWindowModule::getTitleBarMetrics(Runtime &runtime,
                                             TurboModule & /*module*/,
                                             const Value * /*args*/,
                                             size_t /*count*/) {
  return metricsValue(runtime, titleBar().metrics());
}

Value AppKitWindowModule::noop(Runtime & /*runtime*/,
                               TurboModule & /*module*/,
                               const Value * /*args*/,
                               size_t /*count*/) {
  return Value::undefined();
}

} // namespace basalt
