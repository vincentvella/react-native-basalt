// Tests for <TextInput>: the GtkText peer and the controlled-value loop.
//
// The interesting bug in a text field is not typing, it is the loop. React
// Native's <TextInput> is controlled: JavaScript owns the value, the widget
// reports every change, JavaScript re-renders and sends the value back down. If
// applying that prop looks like the user typing, the two chase each other
// forever; if the cursor is not preserved while applying it, the caret jumps
// home on every keystroke. Both are asserted below, because both were wrong
// first.
//
// Props are built through React Native's own RawProps parser rather than by
// assigning fields, because the fields that matter -- `traits.editable`,
// `traits.secureTextEntry` -- are const and only reachable that way. That also
// means these tests exercise the same parse the real thing does.

#include "TestHarness.h"

#include "GtkMountingManager.h"
#include "GtkTextInput.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/core/RawPropsParser.h>

#include <folly/dynamic.h>

using facebook::react::ContextContainer;
using facebook::react::EventEmitter;
using facebook::react::LayoutMetrics;
using facebook::react::PropsParserContext;
using facebook::react::RawProps;
using facebook::react::RawPropsParser;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TextInputProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

// One parser, prepared once: preparing walks every prop TextInputProps knows,
// which is not something to redo per test.
const RawPropsParser &textInputParser() {
  static const RawPropsParser parser = []() {
    RawPropsParser prepared;
    prepared.prepare<TextInputProps>();
    return prepared;
  }();
  return parser;
}

std::shared_ptr<const TextInputProps> makeProps(folly::dynamic raw) {
  static const auto contextContainer = std::make_shared<const ContextContainer>();
  PropsParserContext context{kSurfaceId, *contextContainer};

  RawProps rawProps{std::move(raw)};
  rawProps.parse(textInputParser());
  return std::make_shared<const TextInputProps>(context, TextInputProps{}, rawProps);
}

ShadowView makeTextInput(Tag tag, folly::dynamic raw, float inset = 0.0F) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0.0F, .y = 0.0F}, .size = {.width = 200.0F, .height = 40.0F}};
  metrics.contentInsets = {.left = inset, .top = inset, .right = inset, .bottom = inset};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = makeProps(std::move(raw));
  view.layoutMetrics = metrics;
  return view;
}

// No event emitter: creating a real one needs an EventDispatcher and a runtime
// scheduler, and everything asserted here is on the widget side of the loop.
rnlinux::GtkTextInputManager makeManager() {
  return rnlinux::GtkTextInputManager([](Tag) { return EventEmitter::Shared{}; });
}

const char *textOf(RnView *view) {
  GtkText *editable = rn_view_get_editable(view);
  return editable != nullptr ? gtk_editable_get_text(GTK_EDITABLE(editable)) : nullptr;
}

// What a person typing does, as far as GtkText is concerned: an insertion the
// widget did not come from a prop.
void typeInto(RnView *view, const char *text, int position) {
  int cursor = position;
  gtk_editable_insert_text(
      GTK_EDITABLE(rn_view_get_editable(view)), text, static_cast<int>(strlen(text)), &cursor);
}

} // namespace

TEST(textinput_mounts_a_real_editable) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  EXPECT(rn_view_get_editable(view) == nullptr);
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "hello")));

  // A GtkText, not a PangoLayout with a cursor drawn on it: input methods,
  // selection, the clipboard and every Linux keybinding come with it.
  EXPECT(rn_view_get_editable(view) != nullptr);
  EXPECT(GTK_IS_TEXT(rn_view_get_editable(view)));
  EXPECT_EQ(std::string(textOf(view)), std::string("hello"));

  g_object_unref(view);
}

TEST(textinput_applies_props_without_reporting_them_as_typing) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "a")));
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "ab")));
  EXPECT_EQ(std::string(textOf(view)), std::string("ab"));

  // The event count is the observable half of the loop: applying a prop must
  // not raise it, or the next command from JavaScript looks stale and is
  // dropped. Nothing typed here, so it must still be zero -- which
  // setTextAndSelection at count 0 proves by being accepted.
  EXPECT(manager.dispatchCommand(
      10, "setTextAndSelection", folly::dynamic::array(0, "from js", 0, 0)));
  EXPECT_EQ(std::string(textOf(view)), std::string("from js"));

  g_object_unref(view);
}

TEST(textinput_preserves_the_cursor_when_a_prop_arrives) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abcd")));
  gtk_editable_set_position(GTK_EDITABLE(rn_view_get_editable(view)), 2);

  // A controlled field re-renders on every keystroke, so this happens
  // constantly. Assigning the text resets GtkText's cursor to the start, which
  // sends the caret home mid-word unless it is put back.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "abXcd")));
  EXPECT_EQ(gtk_editable_get_position(GTK_EDITABLE(rn_view_get_editable(view))), 2);

  g_object_unref(view);
}

TEST(textinput_drops_a_command_older_than_what_was_typed) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "")));
  typeInto(view, "fast", 0);

  // The whole reason React Native counts events. JavaScript rendered against an
  // empty field and is only now asking for that state; the user has since
  // typed, so this must be ignored rather than undoing their work.
  EXPECT(manager.dispatchCommand(
      10, "setTextAndSelection", folly::dynamic::array(0, "stale", 0, 0)));
  EXPECT_EQ(std::string(textOf(view)), std::string("fast"));

  // A command that knows about the typing is applied.
  EXPECT(manager.dispatchCommand(
      10, "setTextAndSelection", folly::dynamic::array(99, "current", 0, 0)));
  EXPECT_EQ(std::string(textOf(view)), std::string("current"));

  g_object_unref(view);
}

TEST(textinput_honours_editable_and_secure_entry) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view,
                 makeTextInput(10,
                               folly::dynamic::object("text", "secret")("editable", false)(
                                   "secureTextEntry", true)("placeholder", "type here")));

  GtkText *editable = rn_view_get_editable(view);
  EXPECT(gtk_editable_get_editable(GTK_EDITABLE(editable)) == FALSE);
  EXPECT(gtk_text_get_visibility(editable) == FALSE);
  EXPECT_EQ(std::string(gtk_text_get_placeholder_text(editable)), std::string("type here"));

  // editable is a prop, not a permanent trait; turning it back on must work.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "secret")));
  EXPECT(gtk_editable_get_editable(GTK_EDITABLE(editable)) == TRUE);
  EXPECT(gtk_text_get_visibility(editable) == TRUE);

  g_object_unref(view);
}

TEST(textinput_allocates_its_peer_inside_the_content_inset) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  // Yoga resolves border and padding into the content inset. A GtkText knows
  // nothing about either, so without this the text sits flush against the
  // field's own border and paddingHorizontal does nothing.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "x"), 12.0F));
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 40.0F);
  gtk_widget_allocate(GTK_WIDGET(view), 200, 40, -1, nullptr);

  GtkWidget *peer = GTK_WIDGET(rn_view_get_editable(view));
  EXPECT_EQ(gtk_widget_get_width(peer), 176);
  EXPECT_EQ(gtk_widget_get_height(peer), 16);

  graphene_point_t origin;
  const graphene_point_t zero = {0.0F, 0.0F};
  EXPECT(gtk_widget_compute_point(peer, GTK_WIDGET(view), &zero, &origin));
  EXPECT_NEAR(origin.x, 12.0, 0.5);
  EXPECT_NEAR(origin.y, 12.0, 0.5);

  g_object_unref(view);
}

TEST(textinput_reports_its_value_in_the_widget_tree) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  // The end-to-end suite reads this tree, and a field's text lives in its peer
  // rather than in a PangoLayout, so it would otherwise be invisible there.
  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "in the tree")));
  char *tree = rn_view_describe_tree(view);
  EXPECT(std::string(tree).find("editable=\"in the tree\"") != std::string::npos);
  g_free(tree);

  g_object_unref(view);
}

TEST(textinput_drops_its_peer_when_the_field_is_removed) {
  RnView *view = rn_view_new(10);
  g_object_ref_sink(view);
  auto manager = makeManager();

  manager.update(view, makeTextInput(10, folly::dynamic::object("text", "x")));
  manager.remove(10);

  // The signal handlers hold a pointer into the manager's own map, so the
  // widget has to go with the entry or a later `changed` writes through a
  // dangling pointer.
  EXPECT(rn_view_get_editable(view) == nullptr);
  EXPECT(!manager.dispatchCommand(10, "focus", folly::dynamic::array()));

  g_object_unref(view);
}
