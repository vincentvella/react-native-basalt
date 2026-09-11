// Tests for hit testing and the touch state machine.
//
// Hit testing is the part of input most likely to be quietly wrong, and it is a
// pure function of the view tree, so it can be tested without a mouse, a window
// server, or permission to synthesise an event. That last one matters:
// synthesising a real click on macOS means CGEvent, which needs accessibility
// permission an automated run does not have, so everything below AppKit's event
// delivery is tested here instead.
//
// Unlike the GTK equivalent, none of this needs a window. `gtk_widget_pick`
// skips unmapped widgets, so those tests have to show a window and pump the
// frame clock; `RnAppKitHitTest` walks frames and is answerable straight away.

#include "TestHarness.h"

#import "AppKitTouchDispatcher.h"
#import "RnAppKitView.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <sstream>

using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

RnAppKitView *box(NSInteger tag, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  RnAppKitView *view = [RnAppKitView viewWithTag:tag];
  [view setRnFrameX:x y:y width:w height:h];
  return view;
}

NSInteger hit(RnAppKitView *root, CGFloat x, CGFloat y) {
  RnAppKitView *view = RnAppKitHitTest(root, x, y);
  return view == nil ? 0 : view.rnTag;
}

ShadowView makeView(Tag tag, float x, float y, float width, float height) {
  auto props = std::make_shared<ViewProps>();
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

void apply(basalt::AppKitMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(hit_test_finds_the_view_at_a_point) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 20, 30, 50, 40);
    [root insertRnChild:child atIndex:0];

    EXPECT_EQ((long)hit(root, 25, 35), 10L);
    // Inside the root, outside the child.
    EXPECT_EQ((long)hit(root, 5, 5), 1L);
    // Outside everything.
    EXPECT_EQ((long)hit(root, 500, 500), 0L);
  }
}

// The point is top-left relative, like every other coordinate here. An
// unflipped hit test would find the child 30 from the *bottom* instead, which
// is the same bug `isFlipped` exists to prevent and would be invisible in a
// vertically symmetric layout.
TEST(hit_test_uses_top_left_coordinates) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *top = box(10, 0, 0, 200, 20);
    [root insertRnChild:top atIndex:0];

    EXPECT_EQ((long)hit(root, 100, 10), 10L);
    EXPECT_EQ((long)hit(root, 100, 190), 1L);
  }
}

TEST(hit_test_finds_the_deepest_view) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *outer = box(10, 10, 10, 100, 100);
    RnAppKitView *inner = box(11, 10, 10, 20, 20);
    [root insertRnChild:outer atIndex:0];
    [outer insertRnChild:inner atIndex:0];

    // Coordinates are parent-relative on the way down: (25,25) in the root is
    // (15,15) in outer, which is (5,5) in inner.
    EXPECT_EQ((long)hit(root, 25, 25), 11L);
    EXPECT_EQ((long)hit(root, 50, 50), 10L);
  }
}

// Later siblings draw on top, so they are hit first. An implementation that
// walked the subviews array forwards would return whichever overlapping view
// happened to be added first.
TEST(hit_test_prefers_the_topmost_sibling) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *under = box(10, 0, 0, 100, 100);
    RnAppKitView *over = box(11, 0, 0, 100, 100);
    [root insertRnChild:under atIndex:0];
    [root insertRnChild:over atIndex:1];

    EXPECT_EQ((long)hit(root, 50, 50), 11L);
  }
}

// display: 'none' takes a view out of layout and painting, and it has to come
// out of hit testing too -- otherwise an invisible view swallows presses meant
// for whatever is behind it.
TEST(hit_test_skips_hidden_views) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 0, 0, 100, 100);
    child.hidden = YES;
    [root insertRnChild:child atIndex:0];

    EXPECT_EQ((long)hit(root, 50, 50), 1L);
  }
}

// Hit testing has to follow the scroll, and it does so without knowing what a
// ScrollView is: the point is translated into bounds space, and a scrolled view
// has a non-zero bounds origin. Getting this wrong means a list that scrolls
// correctly and whose rows respond to presses meant for the row above.
TEST(hit_test_follows_the_scroll_offset) {
  @autoreleasepool {
    RnAppKitView *scroller = box(1, 0, 0, 200, 100);
    RnAppKitView *content = box(2, 0, 0, 200, 600);
    RnAppKitView *lower = box(10, 0, 200, 200, 50);
    [scroller insertRnChild:content atIndex:0];
    [content insertRnChild:lower atIndex:0];

    // Unscrolled, the row at y=200 is below the 100-tall viewport.
    EXPECT_EQ((long)hit(scroller, 100, 50), 2L);

    [scroller setRnScrollOffsetX:0 y:200];

    // Scrolled to it, the same screen point is now that row.
    EXPECT_EQ((long)hit(scroller, 100, 10), 10L);
    // And a point past its bottom is the content again.
    EXPECT_EQ((long)hit(scroller, 100, 80), 2L);
  }
}

TEST(hit_test_survives_a_nil_root) {
  @autoreleasepool {
    EXPECT(RnAppKitHitTest(nil, 0, 0) == nil);
  }
}

// The dispatcher against a real mounted tree: a tap has to find the tag the
// mounting manager registered, not just some view.
TEST(a_synthesised_tap_finds_the_mounted_view) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 40, 50, 120, 60)));
    mutations.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 40, 50, 120, 60), 0));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)basalt::hitTestTag(root, 50, 60), 10L);
    EXPECT_EQ((long)basalt::hitTestTag(root, 5, 5), (long)kSurfaceId);

    // No emitter is attached to these hand-built shadow views, so the tap has
    // nowhere to deliver. It must not crash: that is exactly the shape of a
    // press arriving between a Remove and its Delete.
    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    dispatcher.synthesiseTap(50, 60);
    dispatcher.synthesiseTap(5000, 5000);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A move with no button down is hover, and the touch model has no place for it.
// Reporting it would look to the responder system like a finger dragging across
// the screen at all times, which cancels every press before it can fire.
TEST(a_move_before_a_press_is_ignored) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    // Nothing to assert beyond "does not crash and does not start a gesture";
    // the state it must not enter is private, so this pins the behaviour that
    // an end with no start is also a no-op.
    dispatcher.dispatchTouchMove(10, 10);
    dispatcher.dispatchTouchEnd(10, 10);
    dispatcher.dispatchTouchCancel();

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The root forwards mouse events to whatever handler it was given, and a view
// with no handler of its own walks up to find one. Without that, a press on a
// child would reach nothing.
TEST(a_child_forwards_the_mouse_to_the_root_handler) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];
    RnAppKitView *child = box(10, 0, 0, 100, 100);
    [root insertRnChild:child atIndex:0];

    EXPECT(root.rnInputHandler == nil);
    // Its own pool: a weak reference is zeroed in dealloc, and reading one
    // retains and autoreleases the result, so the assertion after the scope
    // only means anything once the pool that saw those has drained.
    @autoreleasepool {
      basalt::AppKitTouchDispatcher dispatcher(&manager, root);
      EXPECT(root.rnInputHandler != nil);
      // The child has none of its own and is expected to find the root's.
      EXPECT(child.rnInputHandler == nil);
    }
    // The dispatcher is gone; the root's reference is weak, so it went with it
    // rather than leaving a dangling handler behind.
    EXPECT(root.rnInputHandler == nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}
