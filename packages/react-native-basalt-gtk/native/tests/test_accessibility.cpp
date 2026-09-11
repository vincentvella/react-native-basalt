// Tests for the accessibility mapping.
//
// GTK ships assertion helpers for exactly this (gtktestatcontext.h), so what a
// screen reader would be told can be read back rather than inferred. That is
// unusual and worth using: accessibility is otherwise the easiest thing in a UI
// to believe you have done.

#include "TestHarness.h"

#include "GtkMountingManager.h"

#include <react/renderer/components/view/ViewProps.h>

#include <sstream>

using facebook::react::AccessibilityState;
using facebook::react::ImportantForAccessibility;
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

// Builds a view with whatever accessibility props the caller wants to set.
ShadowView makeAccessibleView(Tag tag, const std::function<void(ViewProps &)> &configure) {
  auto props = std::make_shared<ViewProps>();
  configure(*props);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 50, .height = 50}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

// Mounts one view and hands back the widget.
RnView *mountOne(basalt::GtkMountingManager &manager, const ShadowView &view) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(view));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, 0));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));

  RnView *root = manager.getSurfaceRoot(kSurfaceId);
  return RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root)));
}

} // namespace

TEST(accessibility_role_maps_to_a_gtk_role) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *button = mountOne(manager, makeAccessibleView(10, [](ViewProps &props) {
                              props.accessibilityRole = "button";
                            }));
  EXPECT_EQ(static_cast<int>(gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(button))),
            static_cast<int>(GTK_ACCESSIBLE_ROLE_BUTTON));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(unknown_accessibility_role_falls_back_to_generic) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // A wrong role is worse than no role: it makes a widget announce itself as
  // something it is not.
  RnView *view = mountOne(manager, makeAccessibleView(11, [](ViewProps &props) {
                            props.accessibilityRole = "somethingReactNativeInvented";
                          }));
  EXPECT_EQ(static_cast<int>(gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(view))),
            static_cast<int>(GTK_ACCESSIBLE_ROLE_GENERIC));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(accessibility_label_and_hint_reach_the_accessible) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(12, [](ViewProps &props) {
                            props.accessibilityLabel = "Play";
                            props.accessibilityHint = "Starts the track";
                          }));

  char *mismatch = gtk_test_accessible_check_property(
      GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_LABEL, "Play");
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  mismatch = gtk_test_accessible_check_property(
      GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Starts the track");
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(accessibility_state_reaches_the_accessible) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(13, [](ViewProps &props) {
                            AccessibilityState state;
                            state.disabled = true;
                            state.selected = true;
                            state.checked = AccessibilityState::Checked;
                            props.accessibilityState = state;
                          }));

  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_DISABLED));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_SELECTED));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_CHECKED));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(no_accessibility_state_leaves_checked_unset) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // A view that never says anything about "checked" is not an unchecked
  // checkbox, and a screen reader should not announce it as one.
  RnView *view = mountOne(manager, makeAccessibleView(14, [](ViewProps &props) { (void)props; }));

  EXPECT(!gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_CHECKED));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(elements_hidden_marks_the_view_hidden) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(15, [](ViewProps &props) {
                            props.accessibilityElementsHidden = true;
                          }));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_HIDDEN));

  RnView *shown = mountOne(manager, makeAccessibleView(16, [](ViewProps &props) { (void)props; }));
  char *mismatch = gtk_test_accessible_check_state(
      GTK_ACCESSIBLE(shown), GTK_ACCESSIBLE_STATE_HIDDEN, FALSE);
  EXPECT(mismatch == nullptr);
  g_free(mismatch);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(important_for_accessibility_no_hide_descendants_hides_too) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountOne(manager, makeAccessibleView(17, [](ViewProps &props) {
                            props.importantForAccessibility =
                                ImportantForAccessibility::NoHideDescendants;
                          }));
  EXPECT(gtk_test_accessible_has_state(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_STATE_HIDDEN));

  manager.destroySurfaceRoot(kSurfaceId);
}
