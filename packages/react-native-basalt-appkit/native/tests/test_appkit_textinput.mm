// Tests for <TextInput>'s controlled-value loop.
//
// The hard part of a text field is not typing. React Native's <TextInput> is a
// controlled component: JavaScript owns the value, the field reports every
// change, JavaScript re-renders and pushes the value back down as a prop. If
// applying that prop looks like the user typing, the two chase each other
// forever; if a prop that arrives late is applied anyway, a fast typist watches
// characters reorder themselves.
//
// Both of those are state machines rather than AppKit, and both are tested
// here through real mutations, because they are only wrong in combination.
//
// Props are built through React Native's own RawProps parser rather than by
// assigning fields: TextInputProps' members are const and only reachable that
// way, which is also how the GTK suite builds them.

#include "TestHarness.h"

#import "AppKitMountingManager.h"
#import "RnAppKitView.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/RawPropsParser.h>

#include <sstream>

using facebook::react::ContextContainer;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::PropsParserContext;
using facebook::react::RawProps;
using facebook::react::RawPropsParser;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TextInputProps;
using facebook::react::TransactionTelemetry;

namespace {

constexpr SurfaceId kSurfaceId = 1;

// One parser, prepared once: preparing walks every prop TextInputProps knows,
// which is not something to redo per test. RawProps has to be parsed before it
// can be read -- constructing one and handing it straight to a Props
// constructor trips an assertion inside RawProps rather than producing empty
// props, which is a better failure than it sounds.
const RawPropsParser &textInputParser() {
  static const RawPropsParser parser = []() {
    RawPropsParser prepared;
    prepared.prepare<TextInputProps>();
    return prepared;
  }();
  return parser;
}

ShadowView makeTextInput(Tag tag, folly::dynamic props) {
  static const auto contextContainer = std::make_shared<const ContextContainer>();
  PropsParserContext context{kSurfaceId, *contextContainer};

  RawProps rawProps{std::move(props)};
  rawProps.parse(textInputParser());
  auto parsed = std::make_shared<const TextInputProps>(context, TextInputProps{}, rawProps);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = parsed;
  view.layoutMetrics = metrics;
  return view;
}

void apply(basalt::AppKitMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

// Mounts one TextInput under the surface root and hands back its view.
RnAppKitView *mountField(basalt::AppKitMountingManager &manager, Tag tag, folly::dynamic props) {
  RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
  [root setRnFrameX:0 y:0 width:400 height:300];

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeTextInput(tag, props)));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeTextInput(tag, props), 0));
  apply(manager, std::move(mutations));

  return manager.viewForTag(tag);
}

NSTextField *fieldOf(RnAppKitView *view) {
  return view.rnEditable;
}

} // namespace

TEST(textinput_mounts_a_real_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 10, folly::dynamic::object("text", "hello"));

    NSTextField *field = fieldOf(view);
    EXPECT(field != nil);
    EXPECT([field.stringValue isEqualToString:@"hello"]);
    // The view behind it draws the background and the border, so the field
    // itself must paint nothing -- an NSTextField's own bezel would sit on top
    // of whatever the style asked for.
    EXPECT(!field.isBordered);
    EXPECT(!field.drawsBackground);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// Applying the `text` prop must not look like the user typing. If it did, the
// change reported would provoke a re-render that sets it again, forever.
TEST(textinput_applying_a_prop_is_not_typing) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 20, folly::dynamic::object("text", "one"));

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(20, folly::dynamic::object("text", "one")),
        makeTextInput(20, folly::dynamic::object("text", "two")),
        kSurfaceId));
    apply(manager, std::move(update));

    EXPECT([fieldOf(view).stringValue isEqualToString:@"two"]);

    // No emitter is attached to these hand-built shadow views, so what this
    // pins is that the round trip did not throw or recurse -- the loop it would
    // enter has no exit, so a failure here is a hang rather than a wrong value.
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The field is placed inside the content inset, so paddingHorizontal on a field
// means what it means on a <View>. Without this the text sits flush against the
// border, ignoring the style.
TEST(textinput_sits_inside_the_content_inset) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowView shadowView = makeTextInput(30, folly::dynamic::object("text", ""));
    shadowView.layoutMetrics.contentInsets = {.left = 12, .top = 4, .right = 12, .bottom = 4};

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(shadowView));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, shadowView, 0));
    apply(manager, std::move(mutations));

    NSTextField *field = fieldOf(manager.viewForTag(30));
    EXPECT(field != nil);
    EXPECT_NEAR(field.frame.origin.x, 12.0, 0.001);
    EXPECT_NEAR(field.frame.origin.y, 4.0, 0.001);
    // 200 wide less 12 either side; 40 tall less 4 top and bottom.
    EXPECT_NEAR(field.frame.size.width, 176.0, 0.001);
    EXPECT_NEAR(field.frame.size.height, 32.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// secureTextEntry is a different *class* on AppKit, not a property, so turning
// it on has to rebuild the field -- and the text has to survive that.
TEST(textinput_secure_entry_rebuilds_the_field) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *view = mountField(manager, 40, folly::dynamic::object("text", "shh"));

    EXPECT(![fieldOf(view) isKindOfClass:[NSSecureTextField class]]);

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeTextInput(40, folly::dynamic::object("text", "shh")),
        makeTextInput(40, folly::dynamic::object("text", "shh")("secureTextEntry", true)),
        kSurfaceId));
    apply(manager, std::move(update));

    EXPECT([fieldOf(view) isKindOfClass:[NSSecureTextField class]]);
    EXPECT([fieldOf(view).stringValue isEqualToString:@"shh"]);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `editable: false` and `readOnly: true` are the same request spelled two ways,
// and React Native honours both.
TEST(textinput_honours_both_spellings_of_read_only) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;

    RnAppKitView *plain = mountField(manager, 50, folly::dynamic::object("text", ""));
    EXPECT(fieldOf(plain).isEditable);
    manager.destroySurfaceRoot(kSurfaceId);

    basalt::AppKitMountingManager second;
    RnAppKitView *locked =
        mountField(second, 51, folly::dynamic::object("text", "")("editable", false));
    EXPECT(!fieldOf(locked).isEditable);
    second.destroySurfaceRoot(kSurfaceId);

    basalt::AppKitMountingManager third;
    RnAppKitView *readOnly =
        mountField(third, 52, folly::dynamic::object("text", "")("readOnly", true));
    EXPECT(!fieldOf(readOnly).isEditable);
    third.destroySurfaceRoot(kSurfaceId);
  }
}

// The field's contents have to show up in the tree dump: they live in the
// NSTextField, not in anything the view draws, so they would otherwise be
// invisible to every test that reads the tree -- and to the cross-platform
// diff, which is where a difference in them would matter most.
TEST(textinput_contents_are_reported_in_the_tree) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowViewMutationList mutations;
    mutations.push_back(
        ShadowViewMutation::CreateMutation(makeTextInput(60, folly::dynamic::object("text", "abc"))));
    mutations.push_back(ShadowViewMutation::InsertMutation(
        kSurfaceId, makeTextInput(60, folly::dynamic::object("text", "abc")), 0));
    apply(manager, std::move(mutations));

    const std::string described = [root describeTree].UTF8String;
    EXPECT(described.find("editable=\"abc\"") != std::string::npos);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A command for a field that was never mounted, and one that is not ours. Both
// have to be survivable: a focus arriving between a Remove and its Delete is
// exactly this shape.
TEST(textinput_commands_survive_an_unknown_tag) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    mountField(manager, 70, folly::dynamic::object("text", ""));

    manager.applyCommand(9999, "focus", folly::dynamic::array());
    manager.applyCommand(70, "somethingElse", folly::dynamic::array());
    manager.applyCommand(70, "blur", folly::dynamic::array());

    manager.destroySurfaceRoot(kSurfaceId);
  }
}
