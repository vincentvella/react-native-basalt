#include "MenuModule.h"

#include "MenuModel.h"
#include "PlatformServices.h"

#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::string stringProperty(Runtime &runtime, const Object &item, const char *name) {
  const Value value = item.getProperty(runtime, name);
  return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
}

std::vector<MenuItemModel> itemsFrom(Runtime &runtime, const Array &array);

MenuItemModel itemFrom(Runtime &runtime, const Object &item) {
  MenuItemModel model;
  model.label = stringProperty(runtime, item, "label");
  model.role = stringProperty(runtime, item, "role");
  model.accelerator = stringProperty(runtime, item, "accelerator");

  const Value separator = item.getProperty(runtime, "separator");
  model.separator = separator.isBool() && separator.asBool();

  const Value enabled = item.getProperty(runtime, "enabled");
  // Absent means enabled: an app listing an item without saying meant to show
  // it, and defaulting the other way would grey out every menu ever written.
  model.enabled = !enabled.isBool() || enabled.asBool();

  const Value id = item.getProperty(runtime, "id");
  model.id = id.isNumber() ? static_cast<int>(id.asNumber()) : 0;

  const Value submenu = item.getProperty(runtime, "submenu");
  if (submenu.isObject() && submenu.asObject(runtime).isArray(runtime)) {
    model.submenu = itemsFrom(runtime, submenu.asObject(runtime).asArray(runtime));
  }
  return model;
}

std::vector<MenuItemModel> itemsFrom(Runtime &runtime, const Array &array) {
  std::vector<MenuItemModel> items;
  const size_t length = array.size(runtime);
  items.reserve(length);
  for (size_t i = 0; i < length; i++) {
    const Value entry = array.getValueAtIndex(runtime, i);
    if (entry.isObject()) {
      items.push_back(itemFrom(runtime, entry.asObject(runtime)));
    }
  }
  return items;
}

} // namespace

DesktopMenuModule::DesktopMenuModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["setApplicationMenu"] = MethodMetadata{1, setMenu};
  methodMap_["isSupported"] = MethodMetadata{0, isSupported};
  // What a NativeEventEmitter over this module calls; the events go out as
  // device events either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};
}

DesktopMenuModule::~DesktopMenuModule() {
  // The handler holds a pointer to this module's emitter. Cleared on the way
  // out, for the same reason the title bar clears its metrics listener.
  setApplicationMenu(MenuModel{}, nullptr);
}

Value DesktopMenuModule::setMenu(Runtime &runtime,
                                 TurboModule &module,
                                 const Value *args,
                                 size_t count) {
  MenuModel model;
  if (count >= 1 && args[0].isObject() && args[0].asObject(runtime).isArray(runtime)) {
    model = itemsFrom(runtime, args[0].asObject(runtime).asArray(runtime));
  }

  auto *self = static_cast<DesktopMenuModule *>(&module);
  // Installed on the UI thread, like everything else that touches a window:
  // this arrives on the JavaScript thread and an NSMenu or an HMENU may not be
  // built there.
  postToUiThread([self, model = std::move(model)] {
    setApplicationMenu(model, [self](int id) {
      self->emitDeviceEvent(kMenuChosenEvent,
                            [id](Runtime & /*runtime*/, std::vector<Value> &args) {
                              args.emplace_back(Value(id));
                            });
    });
  });
  return Value::undefined();
}

Value DesktopMenuModule::isSupported(Runtime & /*runtime*/,
                                     TurboModule & /*module*/,
                                     const Value * /*args*/,
                                     size_t /*count*/) {
  return Value(applicationMenuSupported());
}

Value DesktopMenuModule::noop(Runtime & /*runtime*/,
                              TurboModule & /*module*/,
                              const Value * /*args*/,
                              size_t /*count*/) {
  return Value::undefined();
}

} // namespace basalt
