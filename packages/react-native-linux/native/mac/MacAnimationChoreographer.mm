#import "MacAnimationChoreographer.h"

#import <QuartzCore/QuartzCore.h>

#include <chrono>

// A display link needs an Objective-C target, and the choreographer is a C++
// object, so this carries the callback across.
@interface RnMacDisplayLinkTarget : NSObject
@property(nonatomic, assign) rnlinux::MacAnimationChoreographer *owner;
- (void)step:(CADisplayLink *)sender;
@end

@implementation RnMacDisplayLinkTarget
- (void)step:(CADisplayLink *)sender {
  if (_owner != nullptr) {
    // targetTimestamp rather than timestamp: React Native's animation backend
    // wants the time the frame will be shown, not the time the callback ran.
    _owner->onFrame(sender.targetTimestamp);
  }
}
@end

namespace rnlinux {

namespace {

void onMainThread(dispatch_block_t block) {
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_async(dispatch_get_main_queue(), block);
  }
}

} // namespace

MacAnimationChoreographer::~MacAnimationChoreographer() {
  detach();
}

void MacAnimationChoreographer::attachToView(NSView *view) {
  detach();

  RnMacDisplayLinkTarget *target = [[RnMacDisplayLinkTarget alloc] init];
  target.owner = this;

  // -[NSView displayLinkWithTarget:selector:] is macOS 14 and later. Below
  // that the only alternatives are CVDisplayLink, which is itself deprecated
  // and calls back on its own thread, and a timer, which is not aligned to
  // anything. Neither is worth carrying: this platform targets the current
  // Expo, which is well inside 14.
  if (@available(macOS 14.0, *)) {
    CADisplayLink *link = [view displayLinkWithTarget:target selector:@selector(step:)];
    // Added paused. resume() is what starts it, and starting here would wake
    // the process at display rate for an app that never animates -- the same
    // trade the GTK side makes by not holding the frame clock open.
    link.paused = YES;
    [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    link_ = (__bridge_retained void *)link;
    target_ = (__bridge_retained void *)target;
    syncRunningState();
  } else {
    NSLog(@"react-native: animation frames need macOS 14 or later; Animated and "
          @"requestAnimationFrame will not tick");
  }
}

void MacAnimationChoreographer::detach() {
  if (link_ == nullptr && target_ == nullptr) {
    return;
  }

  void *link = link_;
  void *target = target_;
  link_ = nullptr;
  target_ = nullptr;

  onMainThread(^{
    if (link != nullptr) {
      CADisplayLink *displayLink = (__bridge_transfer CADisplayLink *)link;
      [displayLink invalidate];
    }
    if (target != nullptr) {
      RnMacDisplayLinkTarget *owner = (__bridge_transfer RnMacDisplayLinkTarget *)target;
      owner.owner = nullptr;
    }
  });
}

void MacAnimationChoreographer::resume() {
  running_ = true;
  onMainThread(^{
    syncRunningState();
  });
}

void MacAnimationChoreographer::pause() {
  running_ = false;
  onMainThread(^{
    syncRunningState();
  });
}

void MacAnimationChoreographer::syncRunningState() {
  if (link_ == nullptr) {
    return;
  }
  CADisplayLink *link = (__bridge CADisplayLink *)link_;
  link.paused = running_ ? NO : YES;
}

void MacAnimationChoreographer::onFrame(double timestampSeconds) {
  // AnimationTimestamp is a std::chrono duration against a steady clock, and
  // CADisplayLink's timestamps are mach absolute time in seconds -- the same
  // clock, in different units. Converting rather than calling now() keeps the
  // timestamp the frame's rather than the callback's.
  using namespace std::chrono;
  const auto timestamp = duration_cast<facebook::react::AnimationTimestamp>(
      duration<double>(timestampSeconds));
  onAnimationFrame(timestamp);
}

} // namespace rnlinux
