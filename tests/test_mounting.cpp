// Tests for GtkMountingManager: does a stream of ShadowViewMutations produce
// the widget tree it describes?
//
// This is the layer worth testing hardest. Everything above it is React
// Native's own code, everything below is GTK's, and the mutation walk is where
// this project can be wrong on its own.

#include "TestHarness.h"

#include "GtkMountingManager.h"

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

// Applies mutations synchronously. executeMount would queue onto the main loop,
// which a test has no reason to spin.
void apply(rnlinux::GtkMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

int childCount(RnView *view) {
  int count = 0;
  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(view)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    ++count;
  }
  return count;
}

// The tag of the nth child, or -1.
int childTagAt(RnView *view, int index) {
  int i = 0;
  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(view)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (i == index) {
      return rn_view_get_tag(RN_VIEW(child));
    }
    ++i;
  }
  return -1;
}

} // namespace

TEST(create_and_insert_builds_the_tree) {
  rnlinux::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 0, 0, 100, 50)));
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(11, 0, 60, 100, 50)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 0, 0, 100, 50), 0));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(11, 0, 60, 100, 50), 1));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), 2);
  EXPECT_EQ(childTagAt(root, 0), 10);
  EXPECT_EQ(childTagAt(root, 1), 11);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(insert_honours_the_index) {
  rnlinux::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList first;
  first.push_back(ShadowViewMutation::CreateMutation(makeView(20, 0, 0, 10, 10)));
  first.push_back(ShadowViewMutation::CreateMutation(makeView(21, 0, 0, 10, 10)));
  first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(20, 0, 0, 10, 10), 0));
  first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(21, 0, 0, 10, 10), 1));
  apply(manager, std::move(first));

  // Fabric numbers inserts by their position in the parent's final child list,
  // so a later mutation can land between two existing children.
  ShadowViewMutationList second;
  second.push_back(ShadowViewMutation::CreateMutation(makeView(22, 0, 0, 10, 10)));
  second.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(22, 0, 0, 10, 10), 1));
  apply(manager, std::move(second));

  EXPECT_EQ(childCount(root), 3);
  EXPECT_EQ(childTagAt(root, 0), 20);
  EXPECT_EQ(childTagAt(root, 1), 22);
  EXPECT_EQ(childTagAt(root, 2), 21);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(remove_detaches_but_does_not_destroy) {
  rnlinux::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(30, 0, 0, 10, 10)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList removal;
  removal.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(removal));

  // Detached from the parent...
  EXPECT_EQ(childCount(root), 0);

  // ...but still alive, because a re-Insert may follow before the Delete. This
  // is the invariant the registry's strong reference exists for.
  ShadowViewMutationList reinsert;
  reinsert.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(reinsert));

  EXPECT_EQ(childCount(root), 1);
  EXPECT_EQ(childTagAt(root, 0), 30);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(update_changes_frame_without_reparenting) {
  rnlinux::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(40, 5, 6, 100, 50)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(40, 5, 6, 100, 50), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList update;
  update.push_back(ShadowViewMutation::UpdateMutation(
      makeView(40, 5, 6, 100, 50), makeView(40, 7, 8, 200, 75), kSurfaceId));
  apply(manager, std::move(update));

  EXPECT_EQ(childCount(root), 1);

  graphene_rect_t frame;
  rn_view_get_frame(RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root))), &frame);
  EXPECT_NEAR(frame.origin.x, 7.0, 0.001);
  EXPECT_NEAR(frame.origin.y, 8.0, 0.001);
  EXPECT_NEAR(frame.size.width, 200.0, 0.001);
  EXPECT_NEAR(frame.size.height, 75.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(delete_removes_the_view_from_the_registry) {
  rnlinux::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(50, 0, 0, 10, 10)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList teardown;
  teardown.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  teardown.push_back(ShadowViewMutation::DeleteMutation(makeView(50, 0, 0, 10, 10)));
  apply(manager, std::move(teardown));

  EXPECT_EQ(childCount(root), 0);

  // A stray Insert for a deleted tag must be ignored rather than crash, since
  // that is what a bug upstream would look like from here.
  ShadowViewMutationList stray;
  stray.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  apply(manager, std::move(stray));
  EXPECT_EQ(childCount(root), 0);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(nested_children_are_parented_to_their_own_parent) {
  rnlinux::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(60, 0, 0, 200, 200)));
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(61, 10, 10, 50, 50)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(60, 0, 0, 200, 200), 0));
  // parentTag 60, not the surface root.
  mutations.push_back(ShadowViewMutation::InsertMutation(60, makeView(61, 10, 10, 50, 50), 0));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), 1);
  RnView *outer = RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root)));
  EXPECT_EQ(rn_view_get_tag(outer), 60);
  EXPECT_EQ(childCount(outer), 1);
  EXPECT_EQ(childTagAt(outer, 0), 61);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(surface_root_is_reused_not_recreated) {
  rnlinux::GtkMountingManager manager;
  RnView *first = manager.createSurfaceRoot(kSurfaceId);
  RnView *again = manager.createSurfaceRoot(kSurfaceId);

  // Fabric emits no Create for a root, so asking twice must not produce two.
  EXPECT(first == again);
  EXPECT(manager.getSurfaceRoot(kSurfaceId) == first);

  // In Fabric a SurfaceId *is* the root's tag.
  EXPECT_EQ(rn_view_get_tag(first), static_cast<int>(kSurfaceId));

  manager.destroySurfaceRoot(kSurfaceId);
  EXPECT(manager.getSurfaceRoot(kSurfaceId) == nullptr);
}

TEST(has_component_matches_the_registered_descriptors) {
  rnlinux::GtkMountingManager manager;

  EXPECT(manager.hasComponent("View"));
  EXPECT(manager.hasComponent("RootView"));
  EXPECT(manager.hasComponent("Paragraph"));
  EXPECT(manager.hasComponent("Image"));
  EXPECT(manager.hasComponent("ScrollView"));
  EXPECT(manager.hasComponent("TextInput"));

  // Claiming a component without a GTK peer is worse than admitting the gap:
  // the registry would build shadow nodes nothing can mount.
  EXPECT(!manager.hasComponent("Switch"));
  EXPECT(!manager.hasComponent("Slider"));
}
