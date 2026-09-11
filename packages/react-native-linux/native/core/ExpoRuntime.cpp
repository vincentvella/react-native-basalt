#include "ExpoRuntime.h"

#include "FontRegistry.h"

#include <filesystem>
#include <string>
#include <string_view>

#ifdef RN_LINUX_HAS_EXPO
#include <EventEmitter.h>
#include <JSI/JSIUtils.h>
#include <NativeModule.h>
#include <SharedObject.h>
#include <SharedRef.h>
#endif

namespace rnlinux {

bool hasExpoRuntime() {
#ifdef RN_LINUX_HAS_EXPO
  return true;
#else
  return false;
#endif
}

#ifdef RN_LINUX_HAS_EXPO

namespace {

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::PropNameID;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

// An already-settled promise, which is all this module needs: registering a
// font is synchronous, and the JavaScript signature is async only because it is
// asynchronous everywhere else.
Value resolvedPromise(Runtime &rt) {
  return rt.global()
      .getPropertyAsObject(rt, "Promise")
      .getPropertyAsFunction(rt, "resolve")
      .call(rt);
}

Value rejectedPromise(Runtime &rt, const std::string &message) {
  Function error = rt.global().getPropertyAsFunction(rt, "Error");
  Value reason = error.callAsConstructor(rt, String::createFromUtf8(rt, message));
  return rt.global()
      .getPropertyAsObject(rt, "Promise")
      .getPropertyAsFunction(rt, "reject")
      .call(rt, reason);
}

// expo-font hands over whatever `expo-asset` resolved: a string, or an object
// with a localUri or uri. Both shapes appear depending on how the asset was
// bundled, so both are read rather than one being assumed.
std::string localPathFrom(Runtime &rt, const Value &resource) {
  std::string uri;
  if (resource.isString()) {
    uri = resource.asString(rt).utf8(rt);
  } else if (resource.isObject()) {
    Object object = resource.asObject(rt);
    for (const char *key : {"localUri", "uri"}) {
      Value value = object.getProperty(rt, key);
      if (value.isString()) {
        uri = value.asString(rt).utf8(rt);
        break;
      }
    }
  }

  if (uri.rfind("file://", 0) == 0) {
    uri.erase(0, std::string_view{"file://"}.size());
  }
  return uri;
}

// ExpoAsset, which is how expo-asset turns an asset's URL into a local file.
//
// For a `file://` URL there is nothing to download: React Native resolved it
// beside the bundle and it is already on disk, so the work is confirming that
// and handing the same URL back. The existence check is the point -- without it
// a missing asset succeeds here and fails much later, somewhere that has no
// idea which file was meant.
//
// An http URL is what a dev server produces, and fetching one is not
// implemented. It rejects saying so rather than pretending.
Object makeAssetModule(Runtime &runtime) {
  Object module(runtime);

  module.setProperty(
      runtime,
      "downloadAsync",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "downloadAsync"),
          3,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 1 || !args[0].isString()) {
              return rejectedPromise(rt, "downloadAsync expects a url");
            }
            const std::string url = args[0].asString(rt).utf8(rt);

            if (url.rfind("file://", 0) != 0) {
              return rejectedPromise(
                  rt,
                  "react-native-linux cannot fetch assets over the network yet; "
                  "got " + url);
            }

            const std::string path = url.substr(std::string_view{"file://"}.size());
            std::error_code ignored;
            if (!std::filesystem::is_regular_file(path, ignored)) {
              return rejectedPromise(
                  rt,
                  "no asset at " + path +
                      ". Assets are not copied beside the bundle yet; see plan/16-assets.md.");
            }

            return rt.global()
                .getPropertyAsObject(rt, "Promise")
                .getPropertyAsFunction(rt, "resolve")
                .call(rt, String::createFromUtf8(rt, url));
          }));

  return module;
}

// ExpoFontLoader, as far as a non-web platform is concerned. expo-font's own
// type declaration marks everything but these two as web-only.
Object makeFontLoader(Runtime &runtime) {
  Object loader(runtime);

  loader.setProperty(
      runtime,
      "loadAsync",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "loadAsync"),
          2,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 2 || !args[0].isString()) {
              return rejectedPromise(rt, "loadAsync expects a family name and a font");
            }
            const std::string name = args[0].asString(rt).utf8(rt);
            const std::string path = localPathFrom(rt, args[1]);
            if (path.empty()) {
              return rejectedPromise(
                  rt, "could not find a local file for font '" + name + "'");
            }
            if (!registerFont(name, path)) {
              return rejectedPromise(rt, "could not load font '" + name + "' from " + path);
            }
            return resolvedPromise(rt);
          }));

  loader.setProperty(
      runtime,
      "isLoaded",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "isLoaded"),
          1,
          [](Runtime &rt, const Value &, const Value *args, size_t count) -> Value {
            if (count < 1 || !args[0].isString()) {
              return Value(false);
            }
            return Value(isFontRegistered(args[0].asString(rt).utf8(rt)));
          }));

  loader.setProperty(
      runtime,
      "getLoadedFonts",
      Function::createFromHostFunction(
          runtime,
          PropNameID::forAscii(runtime, "getLoadedFonts"),
          0,
          [](Runtime &rt, const Value &, const Value *, size_t) -> Value {
            Object array = rt.global()
                               .getPropertyAsFunction(rt, "Array")
                               .callAsConstructor(rt)
                               .asObject(rt);
            Function push = array.getPropertyAsFunction(rt, "push");

            const std::string joined = registeredFontNamesJoined('\n');
            std::size_t start = 0;
            while (start < joined.size()) {
              const std::size_t end = joined.find('\n', start);
              const std::string one =
                  joined.substr(start, end == std::string::npos ? std::string::npos : end - start);
              push.callWithThis(rt, array, String::createFromUtf8(rt, one));
              if (end == std::string::npos) {
                break;
              }
              start = end + 1;
            }
            return Value(rt, array);
          }));

  return loader;
}

} // namespace

void installExpoRuntime(facebook::jsi::Runtime &runtime) {
  namespace jsi = facebook::jsi;

  // `expo` has to exist before the classes go in: each of them reaches for
  // `global.expo` through expo::common::getCoreObject and hangs itself off it.
  // Installing them first throws on a property that is not there yet.
  runtime.global().setProperty(runtime, "expo", jsi::Object(runtime));

  expo::EventEmitter::installClass(runtime);

  // The releaser is how Expo hands a shared object's native half back to be
  // freed. On Android it reaches through JNI to the owning context. Nothing
  // here owns any native Expo object -- no Expo module is ported yet -- so
  // there is nothing to free, and the honest implementation of "free nothing"
  // is to do nothing.
  expo::SharedObject::installBaseClass(runtime, [](expo::SharedObject::ObjectId) {});
  expo::SharedRef::installBaseClass(runtime);
  expo::NativeModule::installClass(runtime);

  // The module registry. Empty except for the two modules Expo's own start-up
  // reaches for before an app's code runs at all, and which are therefore not
  // optional the way every other Expo module is.
  //
  // These two are stubs and are meant to be read as stubs. Expo's macOS and
  // Windows ports stub exactly the same pair for exactly the same reason, and
  // the comment in their installer is worth repeating: this is enough for the
  // `require("expo").registerRootComponent(App)` call to survive, no more.
  // Anything that actually calls into them will fail, and should.
  jsi::Object modules(runtime);
  modules.setProperty(runtime, "ExpoAsset", makeAssetModule(runtime));
  modules.setProperty(runtime, "ExponentConstants", jsi::Object(runtime));

  // The first Expo module here that is not a stub. It loads a real font file
  // into fontconfig, which is where Pango looks, so `fontFamily` works
  // afterwards for text this platform renders. See src/LinuxFonts.cpp.
  modules.setProperty(runtime, "ExpoFontLoader", makeFontLoader(runtime));

  // expo-modules-core builds its NativeModulesProxy by iterating this, falling
  // back to the legacy proxy when it is absent, so its presence matters even
  // when it is empty.
  jsi::Object proxy(runtime);
  proxy.setProperty(runtime, "exportedMethods", jsi::Object(runtime));
  proxy.setProperty(runtime, "viewManagersMetadata", jsi::Object(runtime));
  modules.setProperty(runtime, "NativeModulesProxy", std::move(proxy));

  jsi::Object core = runtime.global().getPropertyAsObject(runtime, "expo");
  core.setProperty(runtime, "modules", std::move(modules));
}

#else

void installExpoRuntime(facebook::jsi::Runtime & /*runtime*/) {}

#endif

} // namespace rnlinux
