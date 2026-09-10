#include "LinuxSourceCode.h"

#include <glib.h>

namespace rnlinux {

facebook::jsi::Object LinuxSourceCodeModule::getConstants(facebook::jsi::Runtime &rt) {
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

  char *absolute = g_canonicalize_filename(bundlePath.c_str(), nullptr);
  std::string url = std::string("file://") + absolute;
  g_free(absolute);
  return url;
}

} // namespace rnlinux
