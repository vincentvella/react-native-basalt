#import "AppKitScrollView.h"

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <cmath>

// A wheel notch carries no pixel distance of its own, so a step has to be
// chosen. The same 53 the GTK side uses -- roughly three lines of 16pt text --
// so one notch moves a list by the same amount on both desktops. Matching the
// other platform matters more here than matching either toolkit's own default,
// which is the whole argument of this project in one constant.
static constexpr double kWheelStepPixels = 53.0;

// The bridge between AppKit's protocol and the C++ manager, for the same reason
// the touch dispatcher has one: a C++ object cannot conform to an Objective-C
// protocol, and the views hold their handler weakly.
@interface RnAppKitScrollTarget : NSObject <RnAppKitScrollHandler>
@property(nonatomic, assign) basalt::AppKitScrollViewManager *manager;
@end

@implementation RnAppKitScrollTarget

- (BOOL)rnScrollView:(RnAppKitView *)view
                  by:(NSPoint)delta
             precise:(BOOL)precise
               phase:(NSEventPhase)phase
            momentum:(NSEventPhase)momentum {
  if (_manager == nullptr) {
    return NO;
  }
  // A touchpad reports phases; a wheel reports none. The two streams do not
  // overlap: while the fingers are down `momentum` is NSEventPhaseNone, and
  // once the system is coasting `phase` is. So a drag and the momentum after it
  // are told apart here and nowhere else.
  const bool began = phase == NSEventPhaseBegan;
  const bool ended = phase == NSEventPhaseEnded || phase == NSEventPhaseCancelled;
  const bool momentumBegan = momentum == NSEventPhaseBegan;
  const bool momentumEnded =
      momentum == NSEventPhaseEnded || momentum == NSEventPhaseCancelled;

  // A precise device reports pixels; a wheel reports line counts. Treating a
  // line as a pixel makes the wheel move the content by one pixel a notch,
  // which reads as the wheel not working.
  const double scale = precise ? 1.0 : kWheelStepPixels;

  // AppKit's positive Y is a scroll *up*, which moves content down and reduces
  // the offset. React Native's contentOffset grows downward, like GTK's delta,
  // so both axes are inverted here. scrollingDeltaY already accounts for the
  // user's natural-scrolling preference, so nothing else has to.
  return _manager->scrollBy(static_cast<facebook::react::Tag>(view.rnTag),
                            -delta.x * scale,
                            -delta.y * scale,
                            began,
                            ended,
                            momentumBegan,
                            momentumEnded);
}

@end

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

namespace {

double clampOffset(double value, double content, double container) {
  const double maximum = std::max(0.0, content - container);
  return std::clamp(value, 0.0, maximum);
}

double nowSeconds() {
  return NSProcessInfo.processInfo.systemUptime;
}

} // namespace

AppKitScrollViewManager::AppKitScrollViewManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {
  RnAppKitScrollTarget *target = [[RnAppKitScrollTarget alloc] init];
  target.manager = this;
  scrollTarget_ = target;
}

AppKitScrollViewManager::~AppKitScrollViewManager() {
  ((RnAppKitScrollTarget *)scrollTarget_).manager = nullptr;
  scrollTarget_ = nil;
}

// ---------------------------------------------------------------------------
// Mutation handling
// ---------------------------------------------------------------------------

void AppKitScrollViewManager::update(RnAppKitView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;

  // A ScrollView is the one view that must clip: its content is deliberately
  // larger than its frame, and without this it paints over its siblings.
  [view setRnClipsChildren:YES];
  view.rnScrollHandler = (id<RnAppKitScrollHandler>)scrollTarget_;

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
  [view setRnScrollOffsetX:x y:y];
}

void AppKitScrollViewManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The view's reference to the target is weak and the target dispatches by
  // tag, so dropping the entry is enough: a wheel arriving afterwards finds no
  // entry and is passed up the responder chain instead.
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------

bool AppKitScrollViewManager::scrollBy(Tag tag,
                                       double dx,
                                       double dy,
                                       bool began,
                                       bool ended,
                                       bool momentumBegan,
                                       bool momentumEnded) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (!entry.scrollEnabled) {
    return false;
  }

  if (began && !entry.dragging) {
    entry.dragging = true;
    emitScrollEvent(entry, "beginDrag");
  }
  if (momentumBegan && !entry.coasting) {
    entry.coasting = true;
    emitScrollEvent(entry, "momentumBegin");
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

  // Already in pixels: the trampoline resolved lines against kWheelStepPixels,
  // because only the event knows which it was.
  applyOffset(entry, entry.offsetX + dx, entry.offsetY + dy, true);

  if (ended && entry.dragging) {
    entry.dragging = false;
    emitScrollEvent(entry, "endDrag");
  }
  if (momentumEnded && entry.coasting) {
    entry.coasting = false;
    emitScrollEvent(entry, "momentumEnd");
  }
  return true;
}

// ---------------------------------------------------------------------------
// Offset, events and state
// ---------------------------------------------------------------------------

void AppKitScrollViewManager::applyOffset(Entry &entry, double x, double y, bool emitEvent) {
  const double clampedX = clampOffset(x, entry.contentSize.width, entry.containerSize.width);
  const double clampedY = clampOffset(y, entry.contentSize.height, entry.containerSize.height);

  if (clampedX == entry.offsetX && clampedY == entry.offsetY) {
    return;
  }
  entry.offsetX = clampedX;
  entry.offsetY = clampedY;

  [entry.view setRnScrollOffsetX:clampedX y:clampedY];

  // Unthrottled, and separate from onScroll on purpose.
  // ScrollViewShadowNode::getContentOriginOffset reads this, and through it so
  // do measure, measureLayout, C++ hit testing and view culling. Throttling it
  // would leave all of those reporting a stale scroll position.
  writeStateOffset(entry);

  if (!emitEvent) {
    return;
  }

  // scrollEventThrottle is the platform's job. Zero means every scroll.
  const double now = nowSeconds();
  if (entry.eventThrottleMs > 0) {
    if (now - entry.lastEmitSeconds < entry.eventThrottleMs / 1000.0) {
      return;
    }
  }
  entry.lastEmitSeconds = now;
  emitScrollEvent(entry, "scroll");
}

void AppKitScrollViewManager::emitScrollEvent(Entry &entry, const char *which) {
  const auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(lookup_(entry.tag));
  if (emitter == nullptr) {
    return;
  }

  const auto fill = [&entry](ScrollEvent &event) {
    event.contentOffset =
        Point{.x = static_cast<Float>(entry.offsetX), .y = static_cast<Float>(entry.offsetY)};
    event.contentSize = entry.contentSize;
    event.containerSize = entry.containerSize;
    event.contentInset = entry.contentInset;
    // Not a default worth trusting: ScrollEvent initialises zoomScale to 0, and
    // VirtualizedList only repairs negative values, so leaving it would make
    // every list measurement come out as zero.
    event.zoomScale = 1.0F;
    event.timestamp = static_cast<Float>(nowSeconds());
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
  const std::string_view kind(which);
  if (kind == "beginDrag") {
    emitter->onScrollBeginDrag(event);
  } else if (kind == "momentumBegin") {
    emitter->onMomentumScrollBegin(event);
  } else if (kind == "momentumEnd") {
    emitter->onMomentumScrollEnd(event);
  } else {
    emitter->onScroll(event);
  }
}

void AppKitScrollViewManager::writeStateOffset(const Entry &entry) {
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

bool AppKitScrollViewManager::dispatchCommand(Tag tag,
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
    // There are no indicators to flash yet; see plan/25-macos-scrollview.md.
    return true;
  }

  return false;
}

} // namespace basalt
