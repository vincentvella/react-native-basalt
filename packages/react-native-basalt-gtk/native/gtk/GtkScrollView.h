// <ScrollView> on GTK4.
//
// Yoga does the hard part. A ScrollView's Yoga node carries `overflow: scroll`,
// which lets its child exceed the viewport instead of being clamped to it, so
// by the time a mutation arrives the content is already laid out at its full
// size and the only thing missing is the offset.
//
// Structure: a mounted ScrollView has exactly **one** child, a content view.
// React Native's JS wraps the children in an `RCTScrollContentView`, which the
// C++ registry rewrites to a plain `View` (`componentNameByReactViewName`), so
// no extra descriptor is needed and nothing here should expect N children.
//
// Scrolling is expressed as an offset on the ScrollView's own RnView rather
// than by a GtkScrolledWindow. GtkScrolledWindow wants to size its child
// through the measure/allocate protocol, and this platform's whole invariant is
// that React Native decides sizes and the layout manager only places things.
// Shifting children in `RnLayout::allocate` keeps that invariant and makes GTK's
// hit testing follow the scroll for free.
//
// Two things must happen on every scroll, and they are not the same thing:
//
//   - `onScroll` goes to JavaScript, throttled by `scrollEventThrottle`. Without
//     it VirtualizedList never renders past its first window.
//   - `contentOffset` is written back into `ScrollViewState`, unthrottled.
//     `ScrollViewShadowNode::getContentOriginOffset` reads it, and through that
//     so do `measure`, `measureLayout`, C++ hit testing and view culling. Skip
//     it and those all silently report unscrolled coordinates.

#pragma once

#include "RnView.h"

#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <unordered_map>

namespace basalt {

class GtkScrollViewManager {
 public:
  // Emitters live in the mounting manager's registry, which is also what owns
  // this, so they are reached through a lookup rather than a second copy.
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit GtkScrollViewManager(EmitterLookup lookup);

  // Called for every mutation touching a ScrollView. Attaches the scroll
  // controller the first time, and refreshes the geometry and props after.
  void update(RnView *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // ScrollView's imperative commands: scrollTo, scrollToEnd. Returns false if
  // the command is not one this handles.
  bool dispatchCommand(facebook::react::Tag tag, const std::string &name, const folly::dynamic &args);

 private:
  struct Entry {
    RnView *view{nullptr};
    facebook::react::Tag tag{0};
    GtkScrollViewManager *owner{nullptr};

    // The GTK callbacks below hold a pointer to this Entry. std::unordered_map
    // never moves its elements, so that pointer stays valid for the life of the
    // entry -- but erasing one would leave the controller pointing at freed
    // memory, so remove() detaches the controller first.
    GtkEventController *controller{nullptr};

    std::shared_ptr<const facebook::react::ScrollViewShadowNode::ConcreteState> state;

    facebook::react::Size contentSize{};
    facebook::react::Size containerSize{};
    facebook::react::EdgeInsets contentInset{};

    bool scrollEnabled{true};
    // Milliseconds, as React Native's prop is. Zero means every scroll.
    double eventThrottleMs{0};

    double offsetX{0};
    double offsetY{0};
    gint64 lastEmitMicros{0};
    bool dragging{false};
  };

  static gboolean onScroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer userData);
  static void onScrollBegin(GtkEventControllerScroll *controller, gpointer userData);
  static void onScrollEnd(GtkEventControllerScroll *controller, gpointer userData);

  void applyOffset(Entry &entry, double x, double y, bool emitEvent);
  void emitScrollEvent(Entry &entry, const char *which);
  void writeStateOffset(const Entry &entry);

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;
};

} // namespace basalt
