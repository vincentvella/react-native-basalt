#import "AppKitMountingManager.h"

#import "CoreTextLayout.h"

#include "ComponentRegistry.h"

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/graphics/Color.h>

#include <glog/logging.h>

#include <cassert>
#include <memory>
#include <string_view>
#include <type_traits>

namespace basalt {

// `override` proves each signature matches the interface, but not that every
// pure virtual is implemented -- nothing here instantiates the class. This
// catches an IMountingManager method going unimplemented as the interface
// evolves upstream.
static_assert(!std::is_abstract_v<AppKitMountingManager>,
              "AppKitMountingManager must implement all of IMountingManager");

using facebook::react::ColorComponents;
using facebook::react::ComponentRegistryFactory;
using facebook::react::MountingTransaction;
using facebook::react::ParagraphState;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewProps;

AppKitMountingManager::AppKitMountingManager()
    : scrollViews_([this](Tag tag) { return eventEmitterForTag(tag); }) {}

AppKitMountingManager::~AppKitMountingManager() noexcept {
  // MountingWalk cannot do this itself: by the time a base destructor runs, the
  // AppKit half that knows how to release a view is already gone.
  releaseAllViews();
}

// ---------------------------------------------------------------------------
// Getting onto the main thread
// ---------------------------------------------------------------------------

namespace {

// Carries a transaction from the JS thread to the main thread. A heap struct
// rather than a captured lambda because MountingTransaction is move-only, and
// this is the shape the GTK side uses for the same trip.
struct PendingMount {
  AppKitMountingManager *manager;
  SurfaceId surfaceId;
  MountingTransaction transaction;
};

void applyPendingMount(void *data) {
  std::unique_ptr<PendingMount> pending{static_cast<PendingMount *>(data)};
  pending->manager->applyTransaction(pending->surfaceId, std::move(pending->transaction));
}

} // namespace

void AppKitMountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // This runs on the JS thread: Scheduler::uiManagerDidFinishTransaction queues
  // the mount via RuntimeScheduler::scheduleRenderingUpdate, which drains in the
  // event loop's "update the rendering" step with the jsi::Runtime live. AppKit
  // is main-thread-only -- and not politely so: it asserts and traps the process
  // -- so nothing here may touch a view.
  //
  // Always queue, never invoke directly even when already on the main thread.
  // The main queue is FIFO, which is what keeps mutation ordering intact, and
  // running one transaction inline while another is queued would break it. iOS
  // and Android marshal here too.
  auto *pending = new PendingMount{this, surfaceId, std::move(transaction)};
  dispatch_async_f(dispatch_get_main_queue(), pending, applyPendingMount);
}

void AppKitMountingManager::applyTransaction(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // The walk itself is in core/MountingWalk.h and is shared with GTK; what is
  // AppKit about mounting is below, in the operations it calls back into.
  (void)surfaceId;
  applyMutations(transaction.getMutations());
}

namespace {

// The same trip an executeMount takes, for the same reason: this arrives on the
// JS thread, inside the event loop's rendering update, so it may not touch a
// view either.
//
// Queued on the same queue as a mount, so it stays behind the transaction that
// created the view it names -- the main queue is FIFO, and a scrollTo that
// arrived before its ScrollView was mounted would find no entry and be dropped.
struct PendingCommand {
  AppKitMountingManager *manager;
  Tag tag;
  std::string name;
  folly::dynamic args;
};

void applyPendingCommand(void *data) {
  std::unique_ptr<PendingCommand> pending{static_cast<PendingCommand *>(data)};
  pending->manager->applyCommand(pending->tag, pending->name, pending->args);
}

} // namespace

void AppKitMountingManager::dispatchCommand(const ShadowView &shadowView,
                                            const std::string &commandName,
                                            const folly::dynamic &args) {
  auto *pending = new PendingCommand{this, shadowView.tag, commandName, args};
  dispatch_async_f(dispatch_get_main_queue(), pending, applyPendingCommand);
}

void AppKitMountingManager::applyCommand(Tag tag,
                                         const std::string &commandName,
                                         const folly::dynamic &args) {
  assert(onMainThread() && "applyCommand must run on the main thread");

  if (scrollViews_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  LOG(INFO) << "dispatchCommand '" << commandName << "' on tag " << tag
            << " is not implemented on macOS";
}

ComponentRegistryFactory AppKitMountingManager::getComponentRegistryFactory() {
  return facebook::react::getDefaultComponentRegistryFactory();
}

bool AppKitMountingManager::hasComponent(const std::string &name) {
  // Only what actually mounts. Claiming more would be worse than admitting the
  // gap: the registry would build shadow nodes nothing can put on screen, and
  // the app would render blank rectangles instead of failing somewhere legible.
  //
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString.
  //
  // ScrollView's content child arrives as "ScrollContentView", which the
  // registry rewrites to "View" before it reaches here, so it needs no entry.
  //
  // Image and TextInput are still missing -- an image loader and an NSTextField
  // peer respectively. See plan/25-macos-scrollview.md.
  return name == "View" || name == "RootView" || name == "Paragraph" ||
      name == "ScrollView";
}

// ---------------------------------------------------------------------------
// What MountingWalk asks of a platform
// ---------------------------------------------------------------------------

RnAppKitView *AppKitMountingManager::createView(const ShadowView &shadowView) {
  return [RnAppKitView viewWithTag:static_cast<NSInteger>(shadowView.tag)];
}

RnAppKitView *AppKitMountingManager::createRootView(Tag tag) {
  return [RnAppKitView viewWithTag:static_cast<NSInteger>(tag)];
}

void AppKitMountingManager::destroyView(RnAppKitView *view) {
  // Nothing to do: ARC owns these. The registry holds the only strong reference
  // between a Remove and its Delete, exactly as on GTK, but erasing the entry
  // is what releases it rather than an explicit unref. This hook stays because
  // the walk has to say *when* a view stops being owned, even on a platform
  // where saying it costs nothing.
  (void)view;
}

void AppKitMountingManager::insertChild(RnAppKitView *parent, RnAppKitView *child, int index) {
  [parent insertRnChild:child atIndex:index];
}

void AppKitMountingManager::removeChild(RnAppKitView *parent, RnAppKitView *child) {
  [parent removeRnChild:child];
}

void AppKitMountingManager::forgetTag(Tag tag) {
  scrollViews_.remove(tag);
}

void AppKitMountingManager::updateView(RnAppKitView *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyLayoutMetrics(view, shadowView);
  // Last: the scroll manager clamps its offset against the frame it was just
  // given, and iOS documents the same ordering requirement -- layout before
  // state, or the offset is clamped against a stale size.
  applyScrollView(view, shadowView);
}

void AppKitMountingManager::applyScrollView(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "ScrollView") {
    return;
  }
  scrollViews_.update(view, shadowView);
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a view
// ---------------------------------------------------------------------------

// A <Paragraph> carries its text in state, not props: ParagraphShadowNode
// resolves the whole <Text> subtree into one AttributedString and commits it as
// ParagraphState, which is why nothing here walks child shadow nodes.
void AppKitMountingManager::applyText(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Paragraph") {
    return;
  }

  const auto state =
      std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(shadowView.state);
  if (state == nullptr) {
    return;
  }

  const auto &data = state->getData();
  // Built through the same function the measurement seam uses, which is what
  // makes the painted lines break where the measured ones did. The width is not
  // baked in -- the view draws at whatever size Yoga gave it.
  [view setRnTextLayout:basalt::buildTextLayout(data.attributedString, data.paragraphAttributes)];
}

void AppKitMountingManager::applyProps(RnAppKitView *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const ViewProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  if (props->backgroundColor) {
    const ColorComponents components = colorComponentsFromColor(props->backgroundColor);
    [view setRnBackgroundColorRed:components.red
                            green:components.green
                             blue:components.blue
                            alpha:components.alpha
                         hasColor:YES];
  } else {
    [view setRnBackgroundColorRed:0 green:0 blue:0 alpha:0 hasColor:NO];
  }

  [view setRnOpacity:props->opacity];

  // overflow: 'hidden'. React Native's default is 'visible'.
  [view setRnClipsChildren:props->getClipsContentToBounds()];

  // Radii depend on the frame -- percentage radii, and the clamping that stops
  // opposite corners overlapping -- so they are resolved against the layout
  // metrics rather than read raw.
  const auto borders = props->resolveBorderMetrics(shadowView.layoutMetrics);

  // CALayer has one corner radius; React Native has four, each with its own
  // horizontal and vertical radius. The top-left one is used and the rest
  // ignored, which is right for the overwhelmingly common case of a single
  // `borderRadius` and visibly wrong for anything else. Doing it properly needs
  // a CAShapeLayer mask, which is the same work the GTK side does by hand in a
  // snapshot node.
  [view setRnCornerRadius:borders.borderRadii.topLeft.horizontal];

  // TODO(props): per-corner radii, borders, transform, zIndex, pointerEvents.
  // The GTK side has all of these; none is hard, and each needs a test that
  // compares the result against what Linux produces rather than against what
  // looks plausible on a Mac.
}

void AppKitMountingManager::applyLayoutMetrics(RnAppKitView *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  [view setRnFrameX:frame.origin.x
                  y:frame.origin.y
              width:frame.size.width
             height:frame.size.height];

  // display: 'none' keeps the node in the shadow tree but takes it out of
  // layout and painting.
  view.hidden = shadowView.layoutMetrics.displayType == facebook::react::DisplayType::None;

  // TODO(layout): pointScaleFactor, once a Retina backing store is involved.
}

} // namespace basalt
