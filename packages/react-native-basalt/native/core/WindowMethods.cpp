#include "WindowMethods.h"

#include "PlatformServices.h"
#include "WindowControl.h"

#include <mutex>

namespace basalt {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

bool twoNumbers(const Value *args, size_t count) {
  return count >= 2 && args[0].isNumber() && args[1].isNumber();
}

// Everything that changes a window happens on the UI thread.
//
// A TurboModule method runs on the JavaScript thread, and none of the three
// toolkits may be touched from it. This was found the loud way and reported the
// quiet way: `setSize` from JavaScript did nothing at all on macOS, with no
// warning and no crash.
void onUiThread(std::function<void()> work) {
  postToUiThread(std::move(work));
}

} // namespace

Value windowSetSize(Runtime & /*runtime*/,
                    TurboModule & /*module*/,
                    const Value *args,
                    size_t count) {
  if (twoNumbers(args, count)) {
    const double first = args[0].asNumber();
    const double second = args[1].asNumber();
    onUiThread([first, second] { setWindowSize(first, second); });
  }
  return Value::undefined();
}

Value windowSetPosition(Runtime & /*runtime*/,
                        TurboModule & /*module*/,
                        const Value *args,
                        size_t count) {
  if (twoNumbers(args, count)) {
    const double first = args[0].asNumber();
    const double second = args[1].asNumber();
    onUiThread([first, second] { setWindowPosition(first, second); });
  }
  return Value::undefined();
}

Value windowCenter(Runtime & /*runtime*/,
                   TurboModule & /*module*/,
                   const Value * /*args*/,
                   size_t /*count*/) {
  onUiThread([] { centerWindow(); });
  return Value::undefined();
}

Value windowSetFullScreen(Runtime & /*runtime*/,
                          TurboModule & /*module*/,
                          const Value *args,
                          size_t count) {
  // Anything but a boolean is ignored rather than coerced: `setFullScreen()`
  // with no argument reads as "make it full screen" to a JavaScript truthiness
  // check and as a mistake to everyone else.
  if (count >= 1 && args[0].isBool()) {
    const bool fullScreen = args[0].asBool();
    onUiThread([fullScreen] { setWindowFullScreen(fullScreen); });
  }
  return Value::undefined();
}

Value windowGetBounds(Runtime &runtime,
                      TurboModule & /*module*/,
                      const Value * /*args*/,
                      size_t /*count*/) {
  // From the cache rather than from the toolkit: this runs on the JavaScript
  // thread, and it is read during render, so it can neither ask AppKit nor wait
  // for a hop. The host reports every change, so the cache is exact.
  const WindowBounds bounds = lastKnownWindowBounds();
  Object object(runtime);
  object.setProperty(runtime, "x", bounds.x);
  object.setProperty(runtime, "y", bounds.y);
  object.setProperty(runtime, "width", bounds.width);
  object.setProperty(runtime, "height", bounds.height);
  object.setProperty(runtime, "fullScreen", bounds.fullScreen);
  object.setProperty(runtime, "maximized", bounds.maximized);
  return Value(runtime, object);
}

} // namespace basalt
