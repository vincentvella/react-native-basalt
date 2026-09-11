#include "GtkTouchDispatcher.h"

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

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void GtkTouchDispatcher::dispatchTouchStart(double x, double y) {
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
}

void GtkTouchDispatcher::dispatchTouchMove(double x, double y) {
  // Motion with no button down is hover, which the touch model has no place
  // for. Reporting it would look to the responder system like a finger dragging
  // across the screen at all times.
  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  emit(TouchKind::Move, activeTarget_, x, y);
}

void GtkTouchDispatcher::dispatchTouchEnd(double x, double y) {
  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::End, target, x, y);
}

void GtkTouchDispatcher::dispatchTouchCancel() {
  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, 0, 0);
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
