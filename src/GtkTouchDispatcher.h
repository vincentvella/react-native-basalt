// GTK input into React Native's touch events.
//
// React Native's Pressability -- what backs <Pressable>, <TouchableOpacity> and
// every onPress in an app -- runs on the responder system in JavaScript, and
// the responder system is fed by touchstart/touchmove/touchend. So a pointer on
// a desktop is reported here as a single touch point, which is also what React
// Native for Windows and macOS do. W3C pointer events exist alongside these and
// are only consulted for hover, behind a feature flag.
//
// Controllers are attached to the surface root, not to every view. A GTK
// controller per widget would mean creating and destroying controllers on every
// mutation, and would still need the same hit test: `gtk_widget_pick` already
// walks the widget tree and returns the deepest widget at a point, which is the
// same answer React Native's own hit testing is looking for now that every view
// is allocated at the frame Yoga gave it.

#pragma once

#include "GtkMountingManager.h"
#include "RnView.h"

#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/graphics/Point.h>

namespace rnlinux {

// The tag of the React Native view under a point, in `root`'s coordinates, or 0
// if the point hits nothing.
//
// A free function because it is a pure function of the widget tree and nothing
// else, which is also what makes it testable: hit testing is the part of input
// most likely to be quietly wrong, and it needs no gestures to exercise.
facebook::react::Tag hitTestTag(RnView *root, double x, double y);

class GtkTouchDispatcher {
 public:
  GtkTouchDispatcher(GtkMountingManager *mountingManager, RnView *surfaceRoot);
  ~GtkTouchDispatcher();

  GtkTouchDispatcher(const GtkTouchDispatcher &) = delete;
  GtkTouchDispatcher &operator=(const GtkTouchDispatcher &) = delete;
  GtkTouchDispatcher(GtkTouchDispatcher &&) = delete;
  GtkTouchDispatcher &operator=(GtkTouchDispatcher &&) = delete;

  // Synthesises a press and release at a point in surface-root coordinates,
  // entering at the same place the GTK gesture callbacks do.
  //
  // This exists because the input path is otherwise untestable in automation:
  // synthesising a real pointer event means driving the window system, which on
  // macOS needs accessibility permission a headless run does not have. It skips
  // GDK's event delivery and nothing else, so it proves hit testing, emitter
  // lookup and event-beat delivery, but not that GTK routes clicks here.
  void synthesiseTap(double x, double y);

 private:
  static void onPressed(GtkGestureClick *gesture, int count, double x, double y, gpointer userData);
  static void onReleased(GtkGestureClick *gesture, int count, double x, double y, gpointer userData);
  static void onCancelled(GtkGesture *gesture, GdkEventSequence *sequence, gpointer userData);
  static void onMotion(GtkEventControllerMotion *controller, double x, double y, gpointer userData);

  void dispatchTouchStart(double x, double y);
  void dispatchTouchMove(double x, double y);
  void dispatchTouchEnd(double x, double y);
  void dispatchTouchCancel();

  enum class TouchKind { Start, Move, End, Cancel };

  // Builds the TouchEvent for the current gesture. React Native wants three
  // lists -- all touches, the ones that changed, and the ones that started on
  // the event target -- which with one pointer are the same list, except on
  // touchend where nothing is touching any more.
  void emit(TouchKind kind, facebook::react::Tag target, double x, double y);

  GtkMountingManager *mountingManager_;
  RnView *surfaceRoot_;

  GtkGesture *clickGesture_{nullptr};
  GtkEventController *motionController_{nullptr};

  // The view a gesture started on. React Native reports every touch in a
  // gesture against the target it began on, even after the pointer leaves that
  // view, because that is what the responder system expects.
  facebook::react::Tag activeTarget_{0};
  bool isDown_{false};
};

} // namespace rnlinux
