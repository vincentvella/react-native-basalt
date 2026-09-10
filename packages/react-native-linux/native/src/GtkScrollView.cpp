#include "GtkScrollView.h"

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <cmath>

namespace rnlinux {

using facebook::react::EdgeInsets;
using facebook::react::Float;
using facebook::react::Point;
using facebook::react::ScrollEvent;
using facebook::react::ScrollEndDragEvent;
using facebook::react::ScrollViewEventEmitter;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ShadowView;
using facebook::react::Size;
using facebook::react::Tag;

namespace {

// A wheel notch carries no pixel distance of its own, so a step has to be
// chosen. This is roughly three lines of 16pt text, which is what GTK
// applications and browsers settle on.
constexpr double kWheelStepPixels = 53.0;

double clampOffset(double value, double content, double container) {
  const double maximum = std::max(0.0, content - container);
  return std::clamp(value, 0.0, maximum);
}

} // namespace

GtkScrollViewManager::GtkScrollViewManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

// ---------------------------------------------------------------------------
// Mutation handling
// ---------------------------------------------------------------------------

void GtkScrollViewManager::update(RnView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;
  entry.owner = this;

  // A ScrollView is the one view that must clip: its content is deliberately
  // larger than its frame, and without this it paints over its siblings.
  rn_view_set_clips_children(view, TRUE);

  entry.containerSize = shadowView.layoutMetrics.frame.size;

  if (const auto props = std::dynamic_pointer_cast<const ScrollViewProps>(shadowView.props)) {
    entry.scrollEnabled = props->scrollEnabled;
    entry.contentInset = props->contentInset;
    entry.eventThrottleMs = static_cast<double>(props->scrollEventThrottle);
  }

  if (const auto state =
          std::dynamic_pointer_cast<const ScrollViewShadowNode::ConcreteState>(shadowView.state)) {
    entry.state = state;
    const auto &data = state->getData();
    entry.contentSize = data.getContentSize();

    if (inserted) {
      // First sight of this ScrollView. The state may already carry an offset,
      // from the `contentOffset` prop or from a surface that was suspended and
      // remounted, so adopt it rather than starting at zero.
      entry.offsetX = data.contentOffset.x;
      entry.offsetY = data.contentOffset.y;
    }
  }

  // Re-clamp: the content may have shrunk under a scrolled offset.
  const double x = clampOffset(entry.offsetX, entry.contentSize.width, entry.containerSize.width);
  const double y = clampOffset(entry.offsetY, entry.contentSize.height, entry.containerSize.height);
  entry.offsetX = x;
  entry.offsetY = y;
  rn_view_set_scroll_offset(view, x, y);

  if (inserted) {
    // GTK_EVENT_CONTROLLER_SCROLL_KINETIC is deliberately absent: kinetic
    // deceleration would need momentum events and a velocity model, and
    // reporting a drag that never ends is worse than not reporting momentum.
    entry.controller = gtk_event_controller_scroll_new(
        static_cast<GtkEventControllerScrollFlags>(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES));
    g_signal_connect(entry.controller, "scroll", G_CALLBACK(onScroll), &entry);
    g_signal_connect(entry.controller, "scroll-begin", G_CALLBACK(onScrollBegin), &entry);
    g_signal_connect(entry.controller, "scroll-end", G_CALLBACK(onScrollEnd), &entry);
    gtk_widget_add_controller(GTK_WIDGET(view), entry.controller);
  }
}

void GtkScrollViewManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // Detach before the Entry goes away: the controller's callbacks hold a
  // pointer to it, and gtk_widget_add_controller means the widget, not this,
  // owns the controller's lifetime.
  Entry &entry = it->second;
  if (entry.controller != nullptr && entry.view != nullptr && RN_IS_VIEW(entry.view)) {
    gtk_widget_remove_controller(GTK_WIDGET(entry.view), entry.controller);
  }
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// GTK callbacks
// ---------------------------------------------------------------------------

gboolean GtkScrollViewManager::onScroll(GtkEventControllerScroll *controller,
                                        double dx,
                                        double dy,
                                        gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (!entry->scrollEnabled) {
    return GDK_EVENT_PROPAGATE;
  }

  // A wheel reports discrete notches, a touchpad reports pixels. Treating the
  // first as pixels makes the wheel move the content by one pixel a click.
  double stepX = dx;
  double stepY = dy;
  if (gtk_event_controller_scroll_get_unit(controller) == GDK_SCROLL_UNIT_WHEEL) {
    stepX *= kWheelStepPixels;
    stepY *= kWheelStepPixels;
  }

  entry->owner->applyOffset(*entry, entry->offsetX + stepX, entry->offsetY + stepY, true);
  return GDK_EVENT_STOP;
}

void GtkScrollViewManager::onScrollBegin(GtkEventControllerScroll * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (!entry->scrollEnabled) {
    return;
  }
  entry->dragging = true;
  entry->owner->emitScrollEvent(*entry, "beginDrag");
}

void GtkScrollViewManager::onScrollEnd(GtkEventControllerScroll * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (!entry->dragging) {
    return;
  }
  entry->dragging = false;
  entry->owner->emitScrollEvent(*entry, "endDrag");
}

// ---------------------------------------------------------------------------
// Offset, events and state
// ---------------------------------------------------------------------------

void GtkScrollViewManager::applyOffset(Entry &entry, double x, double y, bool emitEvent) {
  const double clampedX = clampOffset(x, entry.contentSize.width, entry.containerSize.width);
  const double clampedY = clampOffset(y, entry.contentSize.height, entry.containerSize.height);

  if (clampedX == entry.offsetX && clampedY == entry.offsetY) {
    return;
  }
  entry.offsetX = clampedX;
  entry.offsetY = clampedY;

  rn_view_set_scroll_offset(entry.view, clampedX, clampedY);

  // Unthrottled, and separate from onScroll on purpose.
  // ScrollViewShadowNode::getContentOriginOffset reads this, and through it so
  // do measure, measureLayout, C++ hit testing and view culling. Throttling it
  // would leave all of those reporting a stale scroll position.
  writeStateOffset(entry);

  if (!emitEvent) {
    return;
  }

  // scrollEventThrottle is the platform's job. Zero means every scroll.
  const gint64 now = g_get_monotonic_time();
  if (entry.eventThrottleMs > 0) {
    const gint64 interval = static_cast<gint64>(entry.eventThrottleMs * 1000.0);
    if (now - entry.lastEmitMicros < interval) {
      return;
    }
  }
  entry.lastEmitMicros = now;
  emitScrollEvent(entry, "scroll");
}

void GtkScrollViewManager::emitScrollEvent(Entry &entry, const char *which) {
  const auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(lookup_(entry.tag));
  if (emitter == nullptr) {
    return;
  }

  const auto fill = [&entry](ScrollEvent &event) {
    event.contentOffset = Point{.x = static_cast<Float>(entry.offsetX), .y = static_cast<Float>(entry.offsetY)};
    event.contentSize = entry.contentSize;
    event.containerSize = entry.containerSize;
    event.contentInset = entry.contentInset;
    // Not a default worth trusting: ScrollEvent initialises zoomScale to 0, and
    // VirtualizedList only repairs negative values, so leaving it would make
    // every list measurement come out as zero.
    event.zoomScale = 1.0F;
    event.timestamp = static_cast<Float>(g_get_monotonic_time()) / 1e6F;
  };

  if (std::string_view(which) == "endDrag") {
    ScrollEndDragEvent event{};
    fill(event);
    // No velocity model yet, so a drag ends where it stopped. Without a
    // targetContentOffset the JS side treats the drag as ending immediately,
    // which is what actually happens here.
    event.targetContentOffset = event.contentOffset;
    event.velocity = Point{.x = 0, .y = 0};
    emitter->onScrollEndDrag(event);
    return;
  }

  ScrollEvent event{};
  fill(event);
  if (std::string_view(which) == "beginDrag") {
    emitter->onScrollBeginDrag(event);
  } else {
    emitter->onScroll(event);
  }
}

void GtkScrollViewManager::writeStateOffset(const Entry &entry) {
  if (entry.state == nullptr) {
    return;
  }
  const Point offset{.x = static_cast<Float>(entry.offsetX), .y = static_cast<Float>(entry.offsetY)};

  entry.state->updateState(
      [offset](const ScrollViewShadowNode::ConcreteState::Data &oldData)
          -> ScrollViewShadowNode::ConcreteState::SharedData {
        if (oldData.contentOffset == offset) {
          // Returning null cancels the update. The callback can run more than
          // once when commits race, so it must stay a pure function of oldData.
          return nullptr;
        }
        auto newData = oldData;
        newData.contentOffset = offset;
        return std::make_shared<const ScrollViewShadowNode::ConcreteState::Data>(newData);
      });
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool GtkScrollViewManager::dispatchCommand(Tag tag, const std::string &name, const folly::dynamic &args) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;

  if (name == "scrollTo") {
    // [x, y, animated]. Animation is not implemented; the offset is applied at
    // once, which is what `animated: false` asks for anyway.
    if (args.isArray() && args.size() >= 2) {
      applyOffset(entry, args[0].asDouble(), args[1].asDouble(), true);
    }
    return true;
  }

  if (name == "scrollToEnd") {
    applyOffset(entry,
                entry.contentSize.width - entry.containerSize.width,
                entry.contentSize.height - entry.containerSize.height,
                true);
    return true;
  }

  if (name == "flashScrollIndicators") {
    // GTK's overlay scrollbars flash on their own; nothing to do.
    return true;
  }

  return false;
}

} // namespace rnlinux
