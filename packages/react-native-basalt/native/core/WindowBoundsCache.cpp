// The last bounds the host reported, readable from any thread.
//
// The other half of `notifyWindowBoundsChanged`, and the reason `getBounds()`
// can answer without a thread hop: a TurboModule method runs on the JavaScript
// thread, `windowBounds()` may only be called on the UI one, and an app reads
// bounds during render. A cache the host writes on every change is exact, and
// the alternative -- blocking the JavaScript thread on a hop to ask the
// toolkit -- would be a render that waits on a window manager.
//
// Deliberately not in each platform's file: the caching is identical in all
// three and the listener dispatch is what they would each have copied.

#include "WindowControl.h"

#include <mutex>
#include <utility>

namespace basalt {

namespace {

std::mutex &lock() {
  static std::mutex value;
  return value;
}

WindowBounds &cache() {
  static WindowBounds bounds;
  return bounds;
}

std::function<void(const WindowBounds &)> &listener() {
  static std::function<void(const WindowBounds &)> value;
  return value;
}

} // namespace

void setWindowBoundsListener(std::function<void(const WindowBounds &)> value) {
  const std::lock_guard<std::mutex> guard(lock());
  listener() = std::move(value);
}

WindowBounds lastKnownWindowBounds() {
  const std::lock_guard<std::mutex> guard(lock());
  return cache();
}

void notifyWindowBoundsChanged() {
  // Asked on the UI thread, which is where this is called from, and stored
  // where the JavaScript thread can read it.
  const WindowBounds bounds = windowBounds();

  std::function<void(const WindowBounds &)> toCall;
  {
    const std::lock_guard<std::mutex> guard(lock());
    cache() = bounds;
    toCall = listener();
  }
  // Called outside the lock: it emits a device event, and holding a lock across
  // a call into the runtime is how a deadlock is built.
  if (toCall) {
    toCall(bounds);
  }
}

} // namespace basalt
