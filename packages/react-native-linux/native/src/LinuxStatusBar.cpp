#include "LinuxStatusBar.h"

namespace rnlinux {

using facebook::jsi::Object;
using facebook::jsi::Runtime;

Object LinuxStatusBarModule::getConstants(Runtime &rt) {
  Object constants(rt);
  // Zero, and meant literally. React Native's own JavaScript lays out around
  // this value, so reporting a plausible-looking phone height would push every
  // app's content down by a status bar that is not there.
  constants.setProperty(rt, "HEIGHT", 0.0);
  constants.setProperty(rt, "DEFAULT_BACKGROUND_COLOR", 0.0);
  return constants;
}

void LinuxStatusBarModule::setColor(Runtime & /*rt*/, double /*color*/, bool /*animated*/) {}
void LinuxStatusBarModule::setTranslucent(Runtime & /*rt*/, bool /*translucent*/) {}
void LinuxStatusBarModule::setStyle(Runtime & /*rt*/, std::optional<std::string> /*style*/) {}
void LinuxStatusBarModule::setHidden(Runtime & /*rt*/, bool /*hidden*/) {}

} // namespace rnlinux
