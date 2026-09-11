#include "ExpoModules.h"

#include "JsiPromise.h"
#include "PlatformServices.h"

#ifdef BASALT_HAS_EXPO
#include <NativeModule.h>
#endif

#include <folly/json.h>
#include <glog/logging.h>
#include <jsi/JSIDynamic.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace basalt {

namespace {

// Parsed once, at load, so a malformed config is reported when it is read
// rather than when a module happens to ask for it.
std::optional<folly::dynamic> &appConfig() {
  static std::optional<folly::dynamic> config;
  return config;
}

} // namespace

void loadExpoAppConfigBeside(const std::string &bundlePath) {
  std::error_code ignored;
  const std::filesystem::path path =
      std::filesystem::absolute(bundlePath, ignored).parent_path() / "app.config.json";

  std::ifstream file(path);
  if (!file) {
    return;
  }

  std::ostringstream contents;
  contents << file.rdbuf();

  try {
    folly::dynamic parsed = folly::parseJson(contents.str());
    if (!parsed.isObject()) {
      LOG(WARNING) << "expo app config at " << path.string() << " is not an object; ignoring it";
      return;
    }
    appConfig() = std::move(parsed);
  } catch (const std::exception &error) {
    LOG(WARNING) << "could not read the expo app config at " << path.string() << ": "
                 << error.what();
  }
}

#ifdef BASALT_HAS_EXPO

namespace {

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

// A module is one of expo's own `NativeModule` instances rather than a plain
// object: that is where `addListener`, `removeListener` and `emit` come from,
// and expo-linking calls `addListener` whether or not anything ever emits.
Object makeModule(Runtime &runtime) {
  return expo::NativeModule::createInstance(runtime);
}

void addFunction(Runtime &runtime,
                 Object &module,
                 const char *name,
                 unsigned argumentCount,
                 facebook::jsi::HostFunctionType function) {
  module.setProperty(runtime,
                     name,
                     Function::createFromHostFunction(runtime,
                                                      PropNameID::forAscii(runtime, name),
                                                      argumentCount,
                                                      std::move(function)));
}

// ExpoClipboard.
//
// The same clipboard React Native's own `Clipboard` module uses; see
// core/PlatformServices.h. Text only: images and URLs are their own pasteboard
// types on each platform, and the methods for them are deliberately absent so
// that expo-clipboard's own check reports "not available on this platform"
// naming the method, rather than a stub returning null.
Object makeClipboardModule(Runtime &runtime) {
  Object module = makeModule(runtime);

  addFunction(runtime,
              module,
              "getStringAsync",
              1,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, String::createFromUtf8(rt, clipboardText()));
              });

  addFunction(runtime,
              module,
              "setStringAsync",
              2,
              [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
                if (count < 1 || !args[0].isString()) {
                  return rejected(rt, "setStringAsync expects a string");
                }
                setClipboardText(args[0].asString(rt).utf8(rt));
                // The boolean is "did it happen", which it did.
                return resolved(rt, Value(true));
              });

  addFunction(runtime,
              module,
              "hasStringAsync",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, Value(!clipboardText().empty()));
              });

  // iOS-only UI, and expo-clipboard reads it unconditionally on every platform.
  module.setProperty(runtime, "isPasteButtonAvailable", Value(false));

  return module;
}

// ExpoLinking.
//
// Only the two functions expo-linking calls on the native module; `openURL` and
// `canOpenURL` go through React Native's own `Linking`, which this platform
// already implements.
//
// Neither desktop delivers a URL to a running application yet -- macOS needs an
// Apple Event handler and a registered scheme, Linux a desktop entry and a
// single-instance activation -- so `getLinkingURL` is honestly null rather than
// absent: expo-linking calls it without checking, and `useURL()` returning null
// is exactly what "no deep link brought this app up" means.
Object makeLinkingModule(Runtime &runtime) {
  Object module = makeModule(runtime);

  addFunction(runtime,
              module,
              "getLinkingURL",
              0,
              [](Runtime &, const Value &, const Value *, size_t) -> Value {
                return Value::null();
              });

  addFunction(runtime,
              module,
              "clearInitialURL",
              0,
              [](Runtime &, const Value &, const Value *, size_t) -> Value {
                return Value::undefined();
              });

  return module;
}

// ExponentConstants, which is expo-constants' native half.
//
// Two things live here. The manifest is the app's own Expo config, loaded from
// beside the bundle; expo-constants exposes it as `Constants.expoConfig`, and
// treats any object without a `metadata` key as an embedded manifest, which is
// what this is. Everything else is a native constant, and each one here is
// either true or absent -- expo-constants spreads this object into `Constants`,
// so a field invented here is a field an app will read and believe.
//
// `executionEnvironment: 'bare'` is the accurate answer: bare is "a native app
// that happens to use Expo modules", as against running inside Expo Go or a
// dev client, which a desktop host is not.
Object makeConstantsModule(Runtime &runtime) {
  Object module = makeModule(runtime);

  if (appConfig().has_value()) {
    module.setProperty(
        runtime, "manifest", facebook::jsi::valueFromDynamic(runtime, *appConfig()));
  } else {
    // Null rather than absent: expo-constants checks `ExponentConstants.manifest`
    // and warns clearly when it is missing, which is the message a developer
    // needs. Leaving the property off produces the same warning.
    module.setProperty(runtime, "manifest", Value::null());
  }

  module.setProperty(runtime, "executionEnvironment", String::createFromUtf8(runtime, "bare"));
  module.setProperty(runtime, "appOwnership", Value::null());
  module.setProperty(runtime, "expoVersion", Value::null());
  // A desktop is not a simulator, and nothing here reports a status bar.
  module.setProperty(runtime, "isDevice", Value(true));
  module.setProperty(runtime, "statusBarHeight", Value(0));
  // Android and iOS fill this with their own device details. There is no shape
  // defined for a desktop, and inventing one would be inventing a field.
  module.setProperty(runtime, "platform", Object(runtime));

  addFunction(runtime,
              module,
              "getWebViewUserAgentAsync",
              0,
              [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
                return resolved(rt, Value::null());
              });

  return module;
}

} // namespace

void installExpoModules(Runtime &runtime, Object &modules) {
  modules.setProperty(runtime, "ExpoClipboard", makeClipboardModule(runtime));
  modules.setProperty(runtime, "ExpoLinking", makeLinkingModule(runtime));
  // Replaces the empty stub installExpoRuntime puts in for Expo's own start-up.
  modules.setProperty(runtime, "ExponentConstants", makeConstantsModule(runtime));
}

#else

void installExpoModules(facebook::jsi::Runtime & /*runtime*/,
                        facebook::jsi::Object & /*modules*/) {}

#endif

} // namespace basalt
