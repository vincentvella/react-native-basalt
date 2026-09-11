// AppKit input into React Native's touch events.
//
// React Native's Pressability -- what backs <Pressable>, <TouchableOpacity> and
// every onPress in an app -- runs on the responder system in JavaScript, and the
// responder system is fed by touchstart/touchmove/touchend. So a pointer on a
// desktop is reported here as a single touch point, which is also what React
// Native for Windows and macOS do. W3C pointer events exist alongside these and
// are only consulted for hover, behind a feature flag.
//
// Deliberately the same shape as react-native-basalt-gtk's GtkTouchDispatcher,
// down to the state machine and the event construction, because the part that
// is easy to get subtly different -- which touches are in `touches` versus
// `changedTouches`, and which view a gesture is reported against -- is not
// about the toolkit at all. If the two ever diverge, an app's onPress will fire
// on one desktop and not the other.

#pragma once

#import "RnAppKitView.h"

#include "AppKitMountingManager.h"

#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/graphics/Point.h>

namespace basalt {

// The tag of the React Native view under a point, in the root's coordinates, or
// 0 if the point hits nothing. The view-tree half of this is
// `RnAppKitHitTest`, which lives in the view layer and needs no React Native.
facebook::react::Tag hitTestTag(RnAppKitView *root, double x, double y);

class AppKitTouchDispatcher {
 public:
  AppKitTouchDispatcher(AppKitMountingManager *mountingManager, RnAppKitView *surfaceRoot);
  ~AppKitTouchDispatcher();

  AppKitTouchDispatcher(const AppKitTouchDispatcher &) = delete;
  AppKitTouchDispatcher &operator=(const AppKitTouchDispatcher &) = delete;
  AppKitTouchDispatcher(AppKitTouchDispatcher &&) = delete;
  AppKitTouchDispatcher &operator=(AppKitTouchDispatcher &&) = delete;

  // Synthesises a press and release at a point in surface-root coordinates,
  // entering at the same place the AppKit callbacks do.
  //
  // This exists because the input path is otherwise untestable in automation:
  // synthesising a real mouse event on macOS means CGEvent, which needs
  // accessibility permission an automated run does not have. It skips AppKit's
  // event delivery and nothing else, so it proves hit testing, emitter lookup
  // and event-beat delivery, but not that AppKit routes clicks here.
  void synthesiseTap(double x, double y);

  // A press, a run of moves, and a release. The same reason as synthesiseTap,
  // one step further: a gesture recogniser cannot be exercised by a tap at all
  // -- a pan is defined by the movement between the two -- so a drag has to be
  // injectable for anything about it to be provable outside a person's hand.
  void synthesiseDrag(double fromX, double fromY, double toX, double toY, int steps);

  // The main-thread half, called by the view that received the mouse event.
  // Public because the Objective-C trampoline has to reach them.
  void dispatchTouchStart(double x, double y);
  void dispatchTouchMove(double x, double y);
  void dispatchTouchEnd(double x, double y);
  void dispatchTouchCancel();

 private:
  enum class TouchKind { Start, Move, End, Cancel };

  // Hands the pointer to a gesture recogniser that has activated, cancelling
  // React Native's touch. True when that happened, which means the caller has
  // nothing left to report.
  bool yieldToGesture(double x, double y);

  // Builds the TouchEvent for the current gesture. React Native wants three
  // lists -- all touches, the ones that changed, and the ones that started on
  // the event target -- which with one pointer are the same list, except on
  // touchend where nothing is touching any more.
  void emit(TouchKind kind, facebook::react::Tag target, double x, double y);

  AppKitMountingManager *mountingManager_;
  RnAppKitView *surfaceRoot_;
  // The Objective-C object the root forwards mouse events to. Held so it
  // outlives the root's weak reference to it.
  id inputTarget_;

  // The view a gesture started on. React Native reports every touch in a
  // gesture against the target it began on, even after the pointer leaves that
  // view, because that is what the responder system expects.
  facebook::react::Tag activeTarget_{0};
  bool isDown_{false};
};

} // namespace basalt
