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
#include "WindowHost.h"

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

// --- Windows closing themselves ----------------------------------------------
//
// Kept here rather than in each host for the same reason the bounds listener
// is: the storage and the null check are identical in all three, and what
// differs is only which toolkit notices.

namespace {

std::function<void(facebook::react::SurfaceId)> &closedListener() {
  static std::function<void(facebook::react::SurfaceId)> value;
  return value;
}

} // namespace

void setHostWindowClosedListener(std::function<void(facebook::react::SurfaceId)> listener) {
  const std::lock_guard<std::mutex> guard(lock());
  closedListener() = std::move(listener);
}

void hostWindowClosed(facebook::react::SurfaceId surfaceId) {
  std::function<void(facebook::react::SurfaceId)> toCall;
  {
    const std::lock_guard<std::mutex> guard(lock());
    toCall = closedListener();
  }
  // Outside the lock: it emits a device event, and holding a lock across a call
  // into the runtime is how a deadlock is built.
  if (toCall) {
    toCall(surfaceId);
  }
}

} // namespace basalt
