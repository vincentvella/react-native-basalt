// Tests for MacMountingManager: does a stream of ShadowViewMutations produce
// the view tree it describes?
//
// Deliberately the same seven questions `tests/test_mounting.cpp` asks of the
// GTK one, in the same order, with the same tags and frames. The two managers
// share their walk now, so most of this is asking whether the sharing actually
// holds -- and the day it stops holding, these fail here rather than in an app.

#include "TestHarness.h"

#import "MacMountingManager.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <sstream>

using facebook::react::ColorComponents;
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

ShadowView makeView(Tag tag,
                    float x,
                    float y,
                    float width,
                    float height,
                    float red = 0.0F,
                    float green = 0.0F,
                    float blue = 0.0F) {
  auto props = std::make_shared<ViewProps>();
  props->backgroundColor = facebook::react::colorFromComponents(
      ColorComponents{.red = red, .green = green, .blue = blue, .alpha = 1.0F});

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

// Applies mutations synchronously. executeMount would queue onto the main
// queue, which a test has no reason to spin.
void apply(rnlinux::MacMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

NSInteger childCount(RnMacView *view) {
  return (NSInteger)view.subviews.count;
}

// The tag of the nth child, or -1.
NSInteger childTagAt(RnMacView *view, NSInteger index) {
  if (index < 0 || index >= (NSInteger)view.subviews.count) {
    return -1;
  }
  return ((RnMacView *)view.subviews[(NSUInteger)index]).rnTag;
}

} // namespace

TEST(mac_create_and_insert_builds_the_tree) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 0, 0, 100, 50)));
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(11, 0, 60, 100, 50)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 0, 0, 100, 50), 0));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(11, 0, 60, 100, 50), 1));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)childCount(root), 2L);
    EXPECT_EQ((long)childTagAt(root, 0), 10L);
    EXPECT_EQ((long)childTagAt(root, 1), 11L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_insert_honours_the_index) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList first;
    first.push_back(ShadowViewMutation::CreateMutation(makeView(20, 0, 0, 10, 10)));
    first.push_back(ShadowViewMutation::CreateMutation(makeView(21, 0, 0, 10, 10)));
    first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(20, 0, 0, 10, 10), 0));
    first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(21, 0, 0, 10, 10), 1));
    apply(manager, std::move(first));

    // Fabric numbers inserts by their position in the parent's final child
    // list, so a later mutation can land between two existing children. On
    // AppKit that means addSubview:positioned:relativeTo: rather than a plain
    // append, and getting it wrong reorders the z-order too.
    ShadowViewMutationList second;
    second.push_back(ShadowViewMutation::CreateMutation(makeView(22, 0, 0, 10, 10)));
    second.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(22, 0, 0, 10, 10), 1));
    apply(manager, std::move(second));

    EXPECT_EQ((long)childCount(root), 3L);
    EXPECT_EQ((long)childTagAt(root, 0), 20L);
    EXPECT_EQ((long)childTagAt(root, 1), 22L);
    EXPECT_EQ((long)childTagAt(root, 2), 21L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_remove_detaches_but_does_not_destroy) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(30, 0, 0, 10, 10)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
    apply(manager, std::move(mutations));

    ShadowViewMutationList removal;
    removal.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
    apply(manager, std::move(removal));

    // Detached from the parent...
    EXPECT_EQ((long)childCount(root), 0L);

    // ...but still alive, because a re-Insert may follow before the Delete.
    // Under ARC that invariant rests on the registry's map entry rather than on
    // an explicit ref, which is exactly the sort of difference that would go
    // unnoticed until a view vanished mid-reparent.
    ShadowViewMutationList reinsert;
    reinsert.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
    apply(manager, std::move(reinsert));

    EXPECT_EQ((long)childCount(root), 1L);
    EXPECT_EQ((long)childTagAt(root, 0), 30L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_update_changes_frame_without_reparenting) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(40, 5, 6, 100, 50)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(40, 5, 6, 100, 50), 0));
    apply(manager, std::move(mutations));

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeView(40, 5, 6, 100, 50), makeView(40, 7, 8, 200, 75), kSurfaceId));
    apply(manager, std::move(update));

    EXPECT_EQ((long)childCount(root), 1L);

    const NSRect frame = ((RnMacView *)root.subviews[0]).frame;
    EXPECT_NEAR(frame.origin.x, 7.0, 0.001);
    EXPECT_NEAR(frame.origin.y, 8.0, 0.001);
    EXPECT_NEAR(frame.size.width, 200.0, 0.001);
    EXPECT_NEAR(frame.size.height, 75.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_delete_removes_the_view_from_the_registry) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(50, 0, 0, 10, 10)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
    apply(manager, std::move(mutations));

    ShadowViewMutationList teardown;
    teardown.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
    teardown.push_back(ShadowViewMutation::DeleteMutation(makeView(50, 0, 0, 10, 10)));
    apply(manager, std::move(teardown));

    EXPECT_EQ((long)childCount(root), 0L);
    EXPECT(manager.viewForTag(50) == nil);

    // A stray Insert for a deleted tag must be ignored rather than crash, since
    // that is what a bug upstream would look like from here.
    ShadowViewMutationList stray;
    stray.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
    apply(manager, std::move(stray));
    EXPECT_EQ((long)childCount(root), 0L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_nested_children_are_parented_to_their_own_parent) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(60, 0, 0, 200, 200)));
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(61, 10, 10, 50, 50)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(60, 0, 0, 200, 200), 0));
    // parentTag 60, not the surface root.
    mutations.push_back(ShadowViewMutation::InsertMutation(60, makeView(61, 10, 10, 50, 50), 0));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)childCount(root), 1L);
    RnMacView *outer = (RnMacView *)root.subviews[0];
    EXPECT_EQ((long)outer.rnTag, 60L);
    EXPECT_EQ((long)childCount(outer), 1L);
    EXPECT_EQ((long)childTagAt(outer, 0), 61L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_surface_root_is_reused_not_recreated) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *first = manager.createSurfaceRoot(kSurfaceId);
    RnMacView *again = manager.createSurfaceRoot(kSurfaceId);

    // Fabric emits no Create for a root, so asking twice must not produce two.
    EXPECT(first == again);
    EXPECT(manager.getSurfaceRoot(kSurfaceId) == first);

    // In Fabric a SurfaceId *is* the root's tag.
    EXPECT_EQ((long)first.rnTag, (long)kSurfaceId);

    manager.destroySurfaceRoot(kSurfaceId);
    EXPECT(manager.getSurfaceRoot(kSurfaceId) == nil);
  }
}

// Props travel the same path as the tree does, so a mutation that carries only
// new props has to land on the layer without anything else changing.
TEST(mac_props_reach_the_layer_through_a_mutation) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;
    RnMacView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(
        ShadowViewMutation::CreateMutation(makeView(70, 0, 0, 10, 10, 0.25F, 0.5F, 0.75F)));
    mutations.push_back(ShadowViewMutation::InsertMutation(
        kSurfaceId, makeView(70, 0, 0, 10, 10, 0.25F, 0.5F, 0.75F), 0));
    apply(manager, std::move(mutations));

    RnMacView *view = (RnMacView *)root.subviews[0];
    const CGFloat *components = CGColorGetComponents(view.layer.backgroundColor);
    EXPECT_NEAR(components[0], 0.25, 0.01);
    EXPECT_NEAR(components[1], 0.5, 0.01);
    EXPECT_NEAR(components[2], 0.75, 0.01);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(mac_has_component_admits_what_does_not_mount_yet) {
  @autoreleasepool {
    rnlinux::MacMountingManager manager;

    EXPECT(manager.hasComponent("View"));
    EXPECT(manager.hasComponent("RootView"));

    // Everything else. Claiming a component without an AppKit peer is worse
    // than admitting the gap: the registry would build shadow nodes nothing can
    // mount, and the app would render blank rectangles rather than fail.
    EXPECT(!manager.hasComponent("Paragraph"));
    EXPECT(!manager.hasComponent("Image"));
    EXPECT(!manager.hasComponent("ScrollView"));
    EXPECT(!manager.hasComponent("TextInput"));
  }
}
