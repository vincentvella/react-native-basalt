#include "ExpoRuntime.h"

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
  modules.setProperty(runtime, "ExpoAsset", jsi::Object(runtime));
  modules.setProperty(runtime, "ExponentConstants", jsi::Object(runtime));

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
