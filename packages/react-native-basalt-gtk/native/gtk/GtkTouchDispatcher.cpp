#include "GtkTouchDispatcher.h"

#include "Gestures.h"

#include <react/renderer/components/view/TouchEvent.h>

#include <chrono>

namespace basalt {

using facebook::react::HighResTimeStamp;
using facebook::react::Point;
using facebook::react::Tag;
using facebook::react::TouchEventEmitter;
using facebook::react::Touch;
using facebook::react::TouchEvent;
using facebook::react::Touches;

namespace {

// A desktop pointer is one touch point, and React Native identifies touches by
// number. Zero is the first (and here only) finger.
constexpr int kPointerIdentifier = 0;

} // namespace

GtkTouchDispatcher::GtkTouchDispatcher(GtkMountingManager *mountingManager, RnView *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {
  clickGesture_ = gtk_gesture_click_new();
  // Button 0 means every button. React Native has no concept of a right click
  // in its touch model, so they all arrive as touches; which button it was
  // belongs to pointer events, not here.
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickGesture_), 0);
  g_signal_connect(clickGesture_, "pressed", G_CALLBACK(onPressed), this);
  g_signal_connect(clickGesture_, "released", G_CALLBACK(onReleased), this);
  g_signal_connect(clickGesture_, "cancel", G_CALLBACK(onCancelled), this);
  gtk_widget_add_controller(GTK_WIDGET(surfaceRoot_), GTK_EVENT_CONTROLLER(clickGesture_));

  motionController_ = gtk_event_controller_motion_new();
  g_signal_connect(motionController_, "motion", G_CALLBACK(onMotion), this);
  gtk_widget_add_controller(GTK_WIDGET(surfaceRoot_), motionController_);
}

GtkTouchDispatcher::~GtkTouchDispatcher() {
  // gtk_widget_add_controller took ownership, so the controllers die with the
  // widget. Removing them explicitly would be wrong if the root is already gone,
  // and unnecessary if it is not.
}

// ---------------------------------------------------------------------------
// GTK callbacks
// ---------------------------------------------------------------------------

void GtkTouchDispatcher::onPressed(GtkGestureClick * /*gesture*/,
                                   int /*count*/,
                                   double x,
                                   double y,
                                   gpointer userData) {
  static_cast<GtkTouchDispatcher *>(userData)->dispatchTouchStart(x, y);
}

void GtkTouchDispatcher::onReleased(GtkGestureClick * /*gesture*/,
                                    int /*count*/,
                                    double x,
                                    double y,
                                    gpointer userData) {
  static_cast<GtkTouchDispatcher *>(userData)->dispatchTouchEnd(x, y);
}

void GtkTouchDispatcher::onCancelled(GtkGesture * /*gesture*/,
                                     GdkEventSequence * /*sequence*/,
                                     gpointer userData) {
  static_cast<GtkTouchDispatcher *>(userData)->dispatchTouchCancel();
}

void GtkTouchDispatcher::onMotion(GtkEventControllerMotion * /*controller*/,
                                  double x,
                                  double y,
                                  gpointer userData) {
  static_cast<GtkTouchDispatcher *>(userData)->dispatchTouchMove(x, y);
}

void GtkTouchDispatcher::synthesiseTap(double x, double y) {
  dispatchTouchStart(x, y);
  dispatchTouchEnd(x, y);
}

void GtkTouchDispatcher::synthesiseDrag(double fromX, double fromY, double toX, double toY, int steps) {
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

Tag hitTestTag(RnView *root, double x, double y) {
  if (root == nullptr) {
    return 0;
  }
  GtkWidget *picked = gtk_widget_pick(GTK_WIDGET(root), x, y, GTK_PICK_DEFAULT);

  // The deepest widget may not be a React Native view -- and even when every
  // widget is one, walking up is what makes a touch on a child count as a touch
  // on the ancestor that actually handles it.
  while (picked != nullptr && !RN_IS_VIEW(picked)) {
    picked = gtk_widget_get_parent(picked);
  }
  if (picked == nullptr) {
    return 0;
  }
  return static_cast<Tag>(rn_view_get_tag(RN_VIEW(picked)));
}

namespace {

// The React Native views under a point, innermost first, each with its origin
// in the root's coordinates. That is what a gesture recogniser needs and a
// touch does not: a touch is reported against one target, while a gesture may
// be attached to any ancestor of the view that was hit.
//
// Built only when something is attached; see core/Gestures.h.
std::vector<basalt::HitView> hitChain(RnView *root, double x, double y) {
  std::vector<basalt::HitView> chain;
  if (root == nullptr) {
    return chain;
  }

  GtkWidget *picked = gtk_widget_pick(GTK_WIDGET(root), x, y, GTK_PICK_DEFAULT);
  for (GtkWidget *widget = picked; widget != nullptr; widget = gtk_widget_get_parent(widget)) {
    if (RN_IS_VIEW(widget)) {
      // The widget's own origin in the root's coordinates. GTK computes it,
      // rather than this summing frames, so a scrolled ancestor counts.
      graphene_point_t origin{};
      // Not GRAPHENE_POINT_INIT: it is a compound literal, which is C99 rather
      // than C++.
      graphene_point_t zero{};
      zero.x = 0;
      zero.y = 0;
      if (gtk_widget_compute_point(widget, GTK_WIDGET(root), &zero, &origin)) {
        chain.push_back(basalt::HitView{.tag = static_cast<int>(rn_view_get_tag(RN_VIEW(widget))),
                                        .originX = origin.x,
                                        .originY = origin.y});
      }
    }
    if (widget == GTK_WIDGET(root)) {
      break;
    }
  }
  return chain;
}

} // namespace

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void GtkTouchDispatcher::dispatchTouchStart(double x, double y) {
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerDown(
        hitChain(surfaceRoot_, x, y), x, y, basalt::monotonicMilliseconds());
  }

  const Tag target = hitTestTag(surfaceRoot_, x, y);
  g_debug("touch start at (%.0f, %.0f) -> tag %d%s",
          x,
          y,
          static_cast<int>(target),
          mountingManager_->eventEmitterForTag(target) != nullptr ? "" : " (no emitter)");
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

void GtkTouchDispatcher::dispatchTouchMove(double x, double y) {
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

void GtkTouchDispatcher::dispatchTouchEnd(double x, double y) {
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

void GtkTouchDispatcher::dispatchTouchCancel() {
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
// `setJSResponder` does on the platforms it was written for; here both sides
// are fed from this one place, so it is a cancel rather than a negotiation.
bool GtkTouchDispatcher::yieldToGesture(double x, double y) {
  if (!isDown_ || activeTarget_ == 0 || !basalt::gestures().hasActiveHandler()) {
    return false;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, x, y);
  return true;
}

void GtkTouchDispatcher::emit(TouchKind kind, Tag target, double x, double y) {
  const auto emitter =
      std::dynamic_pointer_cast<const TouchEventEmitter>(mountingManager_->eventEmitterForTag(target));
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
