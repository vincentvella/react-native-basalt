#include "Win32WindowModule.h"

#include "PlatformServices.h"
#include "Win32TitleBar.h"

#include <windows.h>

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

// processColor's 0xAARRGGBB number, as the COLORREF DWM takes, or nullopt for
// anything that is not a number -- which is how JavaScript asks for the
// system's colour back. The alpha is dropped: a caption is opaque.
std::optional<COLORREF> colorArgument(const Value *args, size_t count, size_t index) {
  if (index >= count || !args[index].isNumber()) {
    return std::nullopt;
  }
  const auto argb = static_cast<uint32_t>(static_cast<int64_t>(args[index].asNumber()));
  return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

Value metricsValue(Runtime &runtime, const TitleBarMetrics &metrics) {
  Object object(runtime);
  object.setProperty(runtime, "height", static_cast<double>(metrics.height));
  object.setProperty(runtime, "buttonsWidth", static_cast<double>(metrics.buttonsWidth));
  object.setProperty(runtime,
                     "style",
                     String::createFromAscii(
                         runtime, metrics.style == TitleBarStyle::Hidden ? "hidden" : "native"));
  return Value(runtime, object);
}

} // namespace

Win32WindowModule::Win32WindowModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["setTitle"] = MethodMetadata{1, setTitle};
  methodMap_["setTitleBarColors"] = MethodMetadata{3, setTitleBarColors};
  methodMap_["setTitleBarStyle"] = MethodMetadata{1, setTitleBarStyle};
  methodMap_["getTitleBarMetrics"] = MethodMetadata{0, getTitleBarMetrics};
  // What a NativeEventEmitter over this module calls; the events go out as
  // device events either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};

  // Cleared in the destructor, the same arrangement AppearanceModule has with
  // its colour scheme observer.
  titleBar().setMetricsListener([this](const TitleBarMetrics &metrics) {
    emitDeviceEvent(kMetricsChangedEvent, [metrics](Runtime &runtime, std::vector<Value> &args) {
      args.emplace_back(metricsValue(runtime, metrics));
    });
  });
}

Win32WindowModule::~Win32WindowModule() {
  titleBar().setMetricsListener(nullptr);
}

Value Win32WindowModule::setTitle(Runtime &runtime,
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

Value Win32WindowModule::setTitleBarColors(Runtime & /*runtime*/,
                                           TurboModule & /*module*/,
                                           const Value *args,
                                           size_t count) {
  const auto caption = colorArgument(args, count, 0);
  const auto text = colorArgument(args, count, 1);
  const auto border = colorArgument(args, count, 2);
  postToUiThread([caption, text, border] { titleBar().setColors(caption, text, border); });
  return Value::undefined();
}

Value Win32WindowModule::setTitleBarStyle(Runtime &runtime,
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

Value Win32WindowModule::getTitleBarMetrics(Runtime &runtime,
                                            TurboModule & /*module*/,
                                            const Value * /*args*/,
                                            size_t /*count*/) {
  return metricsValue(runtime, titleBar().metrics());
}

Value Win32WindowModule::noop(Runtime & /*runtime*/,
                              TurboModule & /*module*/,
                              const Value * /*args*/,
                              size_t /*count*/) {
  return Value::undefined();
}

} // namespace basalt
