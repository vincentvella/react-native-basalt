#include "GtkTouchDispatcher.h"

#include "Gestures.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/components/view/TouchEvent.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace basalt {

using facebook::react::HighResTimeStamp;
using facebook::react::Point;
using facebook::react::PointerEvent;
using facebook::react::Tag;
using facebook::react::TouchEventEmitter;
using facebook::react::Touch;
using facebook::react::TouchEvent;
using facebook::react::Touches;

namespace {

// A desktop pointer is one touch point, and React Native identifies touches by
// number. Zero is the first (and here only) finger. The same number identifies
// the pointer in pointer events, where React Native keys its hover tracking by
// it -- and where zero is also what React Native's own macOS host uses for a
// mouse.
constexpr int kPointerIdentifier = 0;

// The payload for a hovering pointer, at a point in root coordinates with the
// target's origin in the same space.
PointerEvent hoverEvent(double x, double y, double originX, double originY) {
  PointerEvent event{};
  event.pointerId = kPointerIdentifier;
  event.pointerType = "mouse";
  event.clientPoint = Point{.x = static_cast<facebook::react::Float>(x),
                            .y = static_cast<facebook::react::Float>(y)};
  event.screenPoint = event.clientPoint;
  event.offsetPoint = Point{.x = static_cast<facebook::react::Float>(x - originX),
                            .y = static_cast<facebook::react::Float>(y - originY)};
  event.width = 1;
  event.height = 1;
  // A hovering mouse presses nothing. `button` is -1 rather than 0 because 0 is
  // the left button; -1 is W3C's "no button changed state".
  event.button = -1;
  event.buttons = 0;
  event.isPrimary = true;
  event.timeStamp = HighResTimeStamp::now();
  return event;
}

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
  // The cursor leaving the window is not a motion event, and without it a view
  // that was hovered when the pointer left the window stays hovered forever.
  g_signal_connect(motionController_, "leave", G_CALLBACK(onPointerLeft), this);
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
  auto *self = static_cast<GtkTouchDispatcher *>(userData);
  self->dispatchTouchMove(x, y);
  self->dispatchHover(x, y);
}

void GtkTouchDispatcher::onPointerLeft(GtkEventControllerMotion * /*controller*/, gpointer userData) {
  static_cast<GtkTouchDispatcher *>(userData)->dispatchHoverLeave();
}

void GtkTouchDispatcher::synthesiseTap(double x, double y) {
  dispatchTouchStart(x, y);
  dispatchTouchEnd(x, y);
}

void GtkTouchDispatcher::synthesiseHover(double x, double y) {
  if (x < 0 || y < 0) {
    dispatchHoverLeave();
    return;
  }
  dispatchHover(x, y);
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

namespace {

// `pointerEvents`, which GTK can express one quarter of.
//
// `none` is `can-target`, set on the widget when the prop arrives: GTK's own
// pick then skips the widget and everything inside it, so a press reaches
// whatever is behind. That is the whole of it and nothing here has to help.
//
// The other two have no equivalent, because a GTK widget is targetable or it is
// not and these are neither:
//
//   box-only  the view is a target and nothing inside it is. Resolved by
//             walking up from the pick: the outermost box-only ancestor is the
//             answer, because everything within it has been taken out of hit
//             testing.
//
//   box-none  the view is not a target and everything inside it is. If the
//             pick landed on the view *itself* then nothing inside it was hit,
//             and the press belongs to whatever is behind -- which is what an
//             absolutely-positioned overlay, the reason this mode exists, is
//             asking for. GTK will answer that question, but only about a
//             widget it considers untargetable, so the view is made so for the
//             length of one more pick and put back.
//
// The last part is the only inelegance, and the alternative is worse: a hit
// test of this project's own over the RnView tree, which would have to redo
// the transform and clip handling GTK already does correctly. AppKit and Win32
// own their hit tests and express all three modes directly; this is the same
// answer reached the only way the toolkit allows.
RnView *pickTarget(RnView *root, double x, double y) {
  if (root == nullptr) {
    return nullptr;
  }

  // Bounded: each pass makes exactly one more view untargetable, so a tree of
  // nested box-none views terminates, and a bug here fails a hit test rather
  // than hanging the main loop.
  std::vector<RnView *> suppressed;
  RnView *target = nullptr;
  for (int pass = 0; pass < 16; pass++) {
    GtkWidget *picked = gtk_widget_pick(GTK_WIDGET(root), x, y, GTK_PICK_DEFAULT);

    // The deepest widget may not be a React Native view -- a <TextInput>'s peer
    // is a GtkText -- and even when every widget is one, walking up is what
    // makes a touch on a child count as a touch on the ancestor that handles
    // it.
    while (picked != nullptr && !RN_IS_VIEW(picked)) {
      picked = gtk_widget_get_parent(picked);
    }
    if (picked == nullptr) {
      break;
    }

    // Walk to the root, remembering the outermost box-only. It wins over
    // anything found deeper, because nothing deeper is a target at all.
    RnView *boxOnly = nullptr;
    for (GtkWidget *widget = picked; widget != nullptr; widget = gtk_widget_get_parent(widget)) {
      if (RN_IS_VIEW(widget) &&
          rn_view_get_pointer_events(RN_VIEW(widget)) == RN_POINTER_EVENTS_BOX_ONLY) {
        boxOnly = RN_VIEW(widget);
      }
      if (widget == GTK_WIDGET(root)) {
        break;
      }
    }
    if (boxOnly != nullptr) {
      target = boxOnly;
      break;
    }

    if (rn_view_get_pointer_events(RN_VIEW(picked)) != RN_POINTER_EVENTS_BOX_NONE) {
      target = RN_VIEW(picked);
      break;
    }
    // Transparent, and nothing inside it was hit. Ask again without it.
    gtk_widget_set_can_target(picked, FALSE);
    suppressed.push_back(RN_VIEW(picked));
  }

  for (RnView *view : suppressed) {
    gtk_widget_set_can_target(GTK_WIDGET(view), TRUE);
  }
  return target;
}

} // namespace

Tag hitTestTag(RnView *root, double x, double y) {
  RnView *target = pickTarget(root, x, y);
  return target == nullptr ? 0 : static_cast<Tag>(rn_view_get_tag(target));
}

namespace {

// The React Native views under a point, innermost first, each with its origin
// in the root's coordinates, handed one at a time to `visit`.
//
// Two callers want this walk and want different things out of it -- a gesture
// recogniser wants a HitView, hover wants a HoverView carrying the view's
// listener mask -- so the walk is written once and the shape of the answer is
// the caller's business.
template <typename Visit>
void walkHitChain(RnView *root, double x, double y, Visit &&visit) {
  if (root == nullptr) {
    return;
  }

  // From the resolved target rather than from the raw pick, so that a gesture
  // handler and a hover see the same view a press would. A box-only ancestor
  // that swallows a press swallows a hover too.
  GtkWidget *picked = GTK_WIDGET(pickTarget(root, x, y));
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
        visit(static_cast<int>(rn_view_get_tag(RN_VIEW(widget))), static_cast<double>(origin.x),
              static_cast<double>(origin.y));
      }
    }
    if (widget == GTK_WIDGET(root)) {
      break;
    }
  }
}

// What a gesture recogniser needs and a touch does not: a touch is reported
// against one target, while a gesture may be attached to any ancestor of the
// view that was hit.
//
// Built only when something is attached; see core/Gestures.h.
std::vector<basalt::HitView> hitChain(RnView *root, double x, double y) {
  std::vector<basalt::HitView> chain;
  walkHitChain(root, x, y, [&](int tag, double originX, double originY) {
    chain.push_back(basalt::HitView{.tag = tag, .originX = originX, .originY = originY});
  });
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

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

void GtkTouchDispatcher::dispatchHover(double x, double y) {
  // Only the innermost view is reported against -- React Native's
  // PointerEventsProcessor walks up from it itself -- but the whole chain is
  // still needed, because a listener on any ancestor is reason to dispatch.
  int target = 0;
  double originX = 0;
  double originY = 0;
  std::uint16_t listeners = HoverListenerNone;
  walkHitChain(surfaceRoot_, x, y, [&](int tag, double viewX, double viewY) {
    if (target == 0) {
      target = tag;
      originX = viewX;
      originY = viewY;
    }
    listeners |= mountingManager_->hoverListenersForTag(static_cast<Tag>(tag));
  });

  if (target == 0) {
    dispatchHoverLeave();
    return;
  }
  if (!hover_.admitMove(target, listeners)) {
    return;
  }
  emitPointerMove(target, originX, originY, x, y);
}

void GtkTouchDispatcher::dispatchHoverLeave() {
  const int target = hover_.admitLeave();
  if (target == 0) {
    return;
  }
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(static_cast<Tag>(target)));
  if (emitter == nullptr) {
    return;
  }
  // A leave from a platform is not forwarded: the processor reads it as the
  // pointer being gone and unwinds the path it is holding. So the coordinates
  // on it are never seen by an app, and zero is the honest answer for a
  // position that no longer exists.
  emitter->onPointerLeave(hoverEvent(0, 0, 0, 0));
}

void GtkTouchDispatcher::emitPointerMove(
    facebook::react::Tag target, double originX, double originY, double x, double y) {
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(target));
  if (emitter == nullptr) {
    return;
  }
  emitter->onPointerMove(hoverEvent(x, y, originX, originY));
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
