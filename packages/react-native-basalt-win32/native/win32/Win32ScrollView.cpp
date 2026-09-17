#include "Win32ScrollView.h"

#include "PlatformServices.h"

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace basalt {

using facebook::react::EdgeInsets;
using facebook::react::Float;
using facebook::react::Point;
using facebook::react::ScrollEndDragEvent;
using facebook::react::ScrollEvent;
using facebook::react::ScrollViewEventEmitter;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ShadowView;
using facebook::react::Size;
using facebook::react::Tag;
using win32::RnWin32View;

namespace {

// How long a run of wheel notches may pause before it counts as over. Long
// enough that turning a wheel steadily is one drag rather than a dozen; short
// enough that letting go feels like letting go. Neither other platform needs a
// number here, because both are told.
constexpr double kWheelIdleMs = 150.0;

double clampOffset(double value, double content, double container) {
  const double maximum = std::max(0.0, content - container);
  return std::clamp(value, 0.0, maximum);
}

double nowMilliseconds() {
  const auto since = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double, std::milli>(since).count();
}

} // namespace

Win32ScrollViewManager::Win32ScrollViewManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

Win32ScrollViewManager::~Win32ScrollViewManager() {
  *alive_ = false;
}

// ---------------------------------------------------------------------------
// Mutation handling
// ---------------------------------------------------------------------------

void Win32ScrollViewManager::update(RnWin32View *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;

  // A ScrollView is the one view that must clip: its content is deliberately
  // larger than its frame, and without this it paints over its siblings. Set
  // here rather than left to `overflow`, and after applyProps has run, so that
  // a ScrollView whose props say `visible` still behaves like one.
  view->setClipsChildren(true);

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
  entry.offsetX = clampOffset(entry.offsetX, entry.contentSize.width, entry.containerSize.width);
  entry.offsetY = clampOffset(entry.offsetY, entry.contentSize.height, entry.containerSize.height);
  view->setScrollOffset(static_cast<float>(entry.offsetX), static_cast<float>(entry.offsetY));
}

void Win32ScrollViewManager::remove(Tag tag) {
  // The view is about to be deleted, so the entry must go with it: an idle
  // timer still in flight looks the tag up again and finds nothing, which is
  // the whole reason it is keyed by tag rather than holding the Entry.
  entries_.erase(tag);
}

// ---------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------

bool Win32ScrollViewManager::scrollAt(RnWin32View *root, double x, double y, double dx, double dy) {
  if (root == nullptr || entries_.empty()) {
    return false;
  }
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));

  // Innermost first, which is what makes a list inside a list scroll the inner
  // one. GTK and AppKit get the same ordering from their toolkits; here it is
  // the parent walk.
  for (RnWin32View *view = hit; view != nullptr; view = view->parent()) {
    const auto it = entries_.find(static_cast<Tag>(view->tag()));
    if (it == entries_.end()) {
      continue;
    }
    if (scrollEntry(it->second, dx, dy)) {
      return true;
    }
  }
  return false;
}

// Consumed means "this is a ScrollView and it is enabled", not "it moved" --
// matching AppKit. A list already at its bottom still eats the wheel rather
// than handing it to the list behind it, which is what every desktop does and
// what stops a nested list from dragging its parent along at the end of every
// scroll.
bool Win32ScrollViewManager::scrollEntry(Entry &entry, double dx, double dy) {
  if (!entry.scrollEnabled) {
    return false;
  }

  if (!entry.dragging) {
    entry.dragging = true;
    emitScrollEvent(entry, "beginDrag");
  }

  // The pull. A desktop scroll view has no rubber band to stretch, so what a
  // <RefreshControl> gets instead is the wheel still asking to go up after the
  // offset has already reached zero. Reported before the offset is applied,
  // because applying it changes nothing at the top and there would be nothing
  // left to see.
  if (overscrollTop_) {
    const bool pastTop = dy < 0.0 && entry.offsetY <= 0.0;
    overscrollTop_(entry.tag, pastTop ? -dy : 0.0);
  }

  applyOffset(entry, entry.offsetX + dx, entry.offsetY + dy, true);

  // Last, because with no UI thread installed -- which is every test --
  // postDelayed runs its work inline, and this ends the drag before returning.
  const Tag tag = entry.tag;
  const std::uint64_t generation = ++entry.wheelGeneration;
  postDelayed(kWheelIdleMs, [this, alive = alive_, tag, generation] {
    if (*alive) {
      endWheelDrag(tag, generation);
    }
  });
  return true;
}

void Win32ScrollViewManager::endWheelDrag(Tag tag, std::uint64_t generation) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  Entry &entry = it->second;
  if (entry.wheelGeneration != generation || !entry.dragging) {
    return;
  }
  entry.dragging = false;
  emitScrollEvent(entry, "endDrag");
}

// ---------------------------------------------------------------------------
// Offset, events and state
// ---------------------------------------------------------------------------

void Win32ScrollViewManager::applyOffset(Entry &entry, double x, double y, bool emitEvent) {
  const double clampedX = clampOffset(x, entry.contentSize.width, entry.containerSize.width);
  const double clampedY = clampOffset(y, entry.contentSize.height, entry.containerSize.height);

  if (clampedX == entry.offsetX && clampedY == entry.offsetY) {
    return;
  }
  entry.offsetX = clampedX;
  entry.offsetY = clampedY;

  if (entry.view != nullptr) {
    entry.view->setScrollOffset(static_cast<float>(clampedX), static_cast<float>(clampedY));
  }

  // Unthrottled, and separate from onScroll on purpose.
  // ScrollViewShadowNode::getContentOriginOffset reads this, and through it so
  // do measure, measureLayout, C++ hit testing and view culling. Throttling it
  // would leave all of those reporting a stale scroll position.
  writeStateOffset(entry);

  if (!emitEvent) {
    return;
  }

  // scrollEventThrottle is the platform's job. Zero means every scroll.
  const double now = nowMilliseconds();
  if (entry.eventThrottleMs > 0 && now - entry.lastEmitMs < entry.eventThrottleMs) {
    return;
  }
  entry.lastEmitMs = now;
  emitScrollEvent(entry, "scroll");
}

void Win32ScrollViewManager::emitScrollEvent(Entry &entry, const char *which) {
  const auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(lookup_(entry.tag));
  if (emitter == nullptr) {
    return;
  }

  const auto fill = [&entry](ScrollEvent &event) {
    event.contentOffset = Point{.x = static_cast<Float>(entry.offsetX),
                                .y = static_cast<Float>(entry.offsetY)};
    event.contentSize = entry.contentSize;
    event.containerSize = entry.containerSize;
    event.contentInset = entry.contentInset;
    // Not a default worth trusting: ScrollEvent initialises zoomScale to 0, and
    // VirtualizedList only repairs negative values, so leaving it would make
    // every list measurement come out as zero.
    event.zoomScale = 1.0F;
    event.timestamp = static_cast<Float>(nowMilliseconds() / 1000.0);
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

void Win32ScrollViewManager::writeStateOffset(const Entry &entry) {
  if (entry.state == nullptr) {
    return;
  }
  const Point offset{.x = static_cast<Float>(entry.offsetX),
                     .y = static_cast<Float>(entry.offsetY)};

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

bool Win32ScrollViewManager::dispatchCommand(Tag tag,
                                             const std::string &name,
                                             const folly::dynamic &args) {
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
    // Nothing to flash: this platform draws no scroll indicators at all. A
    // command that is claimed and does nothing beats one that falls through and
    // is logged as unimplemented every time a list settles.
    return true;
  }

  return false;
}

} // namespace basalt
