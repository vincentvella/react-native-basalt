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
#include <set>
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

// --- How big it may be ------------------------------------------------------
//
// Stored here rather than three times, and stored at all rather than simply
// handed to the toolkit, because Windows answers WM_GETMINMAXINFO -- a message
// that arrives whenever the window manager feels like asking, with nowhere to
// have passed the numbers in.

namespace {

WindowSizeLimits &limits() {
  static WindowSizeLimits value;
  return value;
}

} // namespace

void setWindowMinimumSize(double width, double height) {
  {
    const std::lock_guard<std::mutex> guard(lock());
    // Negatives are the same as zero: "no limit". An app clearing a constraint
    // is more likely to pass 0 than -1, and neither should be a window that
    // cannot be smaller than minus one pixel.
    limits().minWidth = width > 0.0 ? width : 0.0;
    limits().minHeight = height > 0.0 ? height : 0.0;
  }
  applyWindowSizeLimits();
}

void setWindowMaximumSize(double width, double height) {
  {
    const std::lock_guard<std::mutex> guard(lock());
    limits().maxWidth = width > 0.0 ? width : 0.0;
    limits().maxHeight = height > 0.0 ? height : 0.0;
  }
  applyWindowSizeLimits();
}

void constrainToWindowSizeLimits(double &width, double &height) {
  // `inForce` rather than `limits`, which is the name of the storage next door.
  const WindowSizeLimits inForce = windowSizeLimits();
  const WindowCapabilities able = windowCapabilities();
  if (able.minimumSize) {
    if (inForce.minWidth > 0.0 && width < inForce.minWidth) {
      width = inForce.minWidth;
    }
    if (inForce.minHeight > 0.0 && height < inForce.minHeight) {
      height = inForce.minHeight;
    }
  }
  if (able.maximumSize) {
    if (inForce.maxWidth > 0.0 && width > inForce.maxWidth) {
      width = inForce.maxWidth;
    }
    if (inForce.maxHeight > 0.0 && height > inForce.maxHeight) {
      height = inForce.maxHeight;
    }
  }
}

WindowSizeLimits windowSizeLimits() {
  const std::lock_guard<std::mutex> guard(lock());
  return limits();
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

// Which windows have an app listening for close attempts, and the listener that
// tells it about one -- the "refusing to close" half below. Declared up here
// because `hostWindowClosed` forgets a window's interception on the way out.
std::set<facebook::react::SurfaceId> &intercepted() {
  static std::set<facebook::react::SurfaceId> value;
  return value;
}

std::function<void(facebook::react::SurfaceId)> &closeRequestListener() {
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
    // A window that is gone cannot refuse anything, and leaving its id behind
    // would mark whichever window is given that surface id next. Surface ids
    // are not reused today; relying on that would be relying on it.
    intercepted().erase(surfaceId);
  }
  // Outside the lock: it emits a device event, and holding a lock across a call
  // into the runtime is how a deadlock is built.
  if (toCall) {
    toCall(surfaceId);
  }
}

// --- Windows refusing to close ----------------------------------------------
//
// Here for the same reason as the rest of this file: three hosts would each
// have written the same set and the same null check, and what differs between
// them is only which toolkit message means "somebody is trying to close this".

void setHostWindowCloseIntercepted(facebook::react::SurfaceId surfaceId, bool value) {
  const std::lock_guard<std::mutex> guard(lock());
  if (value) {
    intercepted().insert(surfaceId);
  } else {
    intercepted().erase(surfaceId);
  }
}

bool hostWindowCloseIntercepted(facebook::react::SurfaceId surfaceId) {
  const std::lock_guard<std::mutex> guard(lock());
  return intercepted().count(surfaceId) != 0;
}

void setHostWindowCloseRequestListener(std::function<void(facebook::react::SurfaceId)> listener) {
  const std::lock_guard<std::mutex> guard(lock());
  closeRequestListener() = std::move(listener);
}

void hostWindowCloseRequested(facebook::react::SurfaceId surfaceId) {
  std::function<void(facebook::react::SurfaceId)> toCall;
  {
    const std::lock_guard<std::mutex> guard(lock());
    toCall = closeRequestListener();
  }
  if (toCall) {
    toCall(surfaceId);
  }
}

} // namespace basalt
