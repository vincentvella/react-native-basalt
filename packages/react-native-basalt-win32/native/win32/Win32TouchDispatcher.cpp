#include "Win32TouchDispatcher.h"

#include "Gestures.h"

#include <react/renderer/components/view/TouchEvent.h>

namespace basalt {

using facebook::react::HighResTimeStamp;
using facebook::react::Point;
using facebook::react::Tag;
using facebook::react::Touch;
using facebook::react::TouchEvent;
using facebook::react::TouchEventEmitter;
using facebook::react::Touches;
using win32::RnWin32View;

namespace {

// A desktop pointer is one touch point, and React Native identifies touches by
// number. Zero is the first (and here only) finger.
constexpr int kPointerIdentifier = 0;

// A point through a 2D affine matrix in Direct2D's Matrix3x2F order and its
// row-vector convention, which is the order `localToParent` writes.
void mapPoint(const float m[6], double x, double y, double &outX, double &outY) {
  const double mappedX = m[0] * x + m[2] * y + m[4];
  const double mappedY = m[1] * x + m[3] * y + m[5];
  outX = mappedX;
  outY = mappedY;
}

// The views under a point, innermost first, each with its origin in the
// surface root's coordinates. That is what a gesture recogniser needs and a
// touch does not: a touch is reported against one target, while a gesture may
// be attached to any ancestor of the view that was hit.
//
// The origin is carried *up* the chain rather than recomputed per view, so the
// walk stays linear and each view's contribution -- its own transform, then its
// parent's scroll offset -- is applied exactly once. AppKit gets the same
// answer from convertPoint:toView: and GTK from the widget's allocation; here
// there is no toolkit geometry, so the composition is written out. It is the
// mirror of what `hitTest` did on the way down, which is the only thing that
// makes the two agree about a rotated or scrolled ancestor.
//
// Built only when something is attached; see core/Gestures.h.
std::vector<basalt::HitView> hitChain(RnWin32View *root, double x, double y) {
  std::vector<basalt::HitView> chain;
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  if (hit == nullptr) {
    return chain;
  }

  double originX = 0.0;
  double originY = 0.0;
  for (RnWin32View *view = hit; view != nullptr; view = view->parent()) {
    chain.push_back(basalt::HitView{
        .tag = static_cast<int>(view->tag()), .originX = originX, .originY = originY});
    if (view == root) {
      break;
    }
    RnWin32View *parent = view->parent();
    if (parent == nullptr) {
      break;
    }
    float local[6];
    view->localToParent(local);
    mapPoint(local, originX, originY, originX, originY);
    originX -= parent->scrollX();
    originY -= parent->scrollY();
  }
  return chain;
}

} // namespace

Win32TouchDispatcher::Win32TouchDispatcher(
    Win32MountingManager *mountingManager,
    RnWin32View *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {}

void Win32TouchDispatcher::synthesiseTap(double x, double y) {
  dispatchTouchStart(x, y);
  dispatchTouchEnd(x, y);
}

void Win32TouchDispatcher::synthesiseDrag(
    double fromX, double fromY, double toX, double toY, int steps) {
  dispatchTouchStart(fromX, fromY);
  const int count = steps < 1 ? 1 : steps;
  for (int step = 1; step <= count; step++) {
    const double progress = static_cast<double>(step) / count;
    dispatchTouchMove(fromX + (toX - fromX) * progress, fromY + (toY - fromY) * progress);
  }
  dispatchTouchEnd(toX, toY);
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

Tag hitTestTag(RnWin32View *root, double x, double y) {
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  return hit == nullptr ? 0 : static_cast<Tag>(hit->tag());
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void Win32TouchDispatcher::dispatchTouchStart(double x, double y) {
  if (surfaceRoot_ == nullptr) {
    return;
  }
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerDown(
        hitChain(surfaceRoot_, x, y), x, y, basalt::monotonicMilliseconds());
  }

  const Tag target = hitTestTag(surfaceRoot_, x, y);
  if (target == 0) {
    return;
  }
  activeTarget_ = target;
  isDown_ = true;
  emit(TouchKind::Start, target, x, y);

  // A gesture that claimed the pointer on contact -- a native handler -- takes
  // it away from React Native's responder system immediately.
  yieldToGesture(x, y);
}

void Win32TouchDispatcher::dispatchTouchMove(double x, double y) {
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerMove(x, y, basalt::monotonicMilliseconds());
  }

  // Motion with no button down is hover, which the touch model has no place
  // for. Reporting it would look to the responder system like a finger dragging
  // across the screen at all times.
  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  if (yieldToGesture(x, y)) {
    return;
  }
  emit(TouchKind::Move, activeTarget_, x, y);
}

void Win32TouchDispatcher::dispatchTouchEnd(double x, double y) {
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerUp(x, y, basalt::monotonicMilliseconds());
  }

  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::End, target, x, y);
}

void Win32TouchDispatcher::dispatchTouchCancel() {
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerCancel();
  }

  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, 0, 0);
}

// A gesture recogniser that has activated owns the pointer, and React Native's
// responder system must be told the touch it was following is gone -- otherwise
// panning across a <Pressable> pans *and* presses it. This is what RNGH's
// setJSResponder does on the platforms it was written for; here both sides are
// fed from this one place, so it is a cancel rather than a negotiation.
bool Win32TouchDispatcher::yieldToGesture(double x, double y) {
  if (!isDown_ || activeTarget_ == 0 || !basalt::gestures().hasActiveHandler()) {
    return false;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, x, y);
  return true;
}

void Win32TouchDispatcher::emit(TouchKind kind, Tag target, double x, double y) {
  if (mountingManager_ == nullptr) {
    return;
  }
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(target));
  if (emitter == nullptr) {
    return;
  }

  const auto now = HighResTimeStamp::now();

  Touch touch{};
  touch.identifier = kPointerIdentifier;
  touch.target = target;
  // Coordinates arrive relative to the surface root, which is what React Native
  // calls the page. offsetPoint should be relative to the target view; until
  // there is a cheap way to get the target's absolute origin, page coordinates
  // are the honest approximation. Pressability does not read offsetPoint.
  touch.pagePoint = Point{.x = static_cast<facebook::react::Float>(x),
                          .y = static_cast<facebook::react::Float>(y)};
  touch.screenPoint = touch.pagePoint;
  touch.offsetPoint = touch.pagePoint;
  touch.force = 1.0F;
  touch.timeStamp = now;

  Touches changed{};
  changed.insert(touch);

  const bool isEnd = kind == TouchKind::End || kind == TouchKind::Cancel;

  TouchEvent event{};
  event.changedTouches = changed;
  // On touchend the finger is gone, so it is no longer in `touches`. Getting
  // this wrong leaves the responder system believing a touch is still active,
  // which swallows the next press.
  event.touches = isEnd ? Touches{} : changed;
  event.targetTouches = event.touches;

  switch (kind) {
    case TouchKind::Start:
      emitter->onTouchStart(event);
      break;
    case TouchKind::Move:
      emitter->onTouchMove(event);
      break;
    case TouchKind::End:
      emitter->onTouchEnd(event);
      break;
    case TouchKind::Cancel:
      emitter->onTouchCancel(event);
      break;
  }
}

} // namespace basalt
