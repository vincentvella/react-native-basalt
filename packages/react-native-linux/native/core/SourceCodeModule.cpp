#include "SourceCodeModule.h"

#include <filesystem>

namespace rnlinux {

facebook::jsi::Object DesktopSourceCodeModule::getConstants(facebook::jsi::Runtime &rt) {
  facebook::jsi::Object constants(rt);
  constants.setProperty(rt, "scriptURL", facebook::jsi::String::createFromUtf8(rt, scriptURL_));
  return constants;
}

std::string scriptURLFor(const std::string &bundlePath,
                         bool devMode,
                         const std::string &devHost,
                         unsigned int devPort,
                         const std::string &devEntry) {
  if (devMode) {
    // Shaped like the URL the dev server actually served, because React Native
    // recovers the server's address by stripping the path off this. Assets then
    // resolve to that server, which is what makes them work in development
    // without anything being copied anywhere.
    return "http://" + devHost + ":" + std::to_string(devPort) + "/" + devEntry +
        ".bundle?platform=linux&dev=true";
  }

  // Absolute and fully formed: React Native tests this with
  // startsWith('file://') and then takes the directory beside it, so a relative
  // path silently produces an asset URL that resolves nowhere.
  std::error_code ignored;
  const std::filesystem::path absolute = std::filesystem::absolute(bundlePath, ignored);
  return "file://" + absolute.lexically_normal().string();
}

} // namespace rnlinux
