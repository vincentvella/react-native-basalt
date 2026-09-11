#import "MacMountingManager.h"

#include "ComponentRegistry.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <glog/logging.h>

#include <cassert>
#include <memory>
#include <string_view>
#include <type_traits>

namespace rnlinux {

// `override` proves each signature matches the interface, but not that every
// pure virtual is implemented -- nothing here instantiates the class. This
// catches an IMountingManager method going unimplemented as the interface
// evolves upstream.
static_assert(!std::is_abstract_v<MacMountingManager>,
              "MacMountingManager must implement all of IMountingManager");

using facebook::react::ColorComponents;
using facebook::react::ComponentRegistryFactory;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewProps;

MacMountingManager::~MacMountingManager() noexcept {
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
  MacMountingManager *manager;
  SurfaceId surfaceId;
  MountingTransaction transaction;
};

void applyPendingMount(void *data) {
  std::unique_ptr<PendingMount> pending{static_cast<PendingMount *>(data)};
  pending->manager->applyTransaction(pending->surfaceId, std::move(pending->transaction));
}

} // namespace

void MacMountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
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

void MacMountingManager::applyTransaction(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // The walk itself is in core/MountingWalk.h and is shared with GTK; what is
  // AppKit about mounting is below, in the operations it calls back into.
  (void)surfaceId;
  applyMutations(transaction.getMutations());
}

void MacMountingManager::dispatchCommand(const ShadowView &shadowView,
                                         const std::string &commandName,
                                         const folly::dynamic &args) {
  (void)args;
  // Nothing on this platform takes a command yet: the components that do --
  // ScrollView's scrollTo, TextInput's focus and blur -- have no AppKit peer.
  // When they arrive this needs the same marshalling executeMount does, for the
  // same reason and then some: `focus` reaches the input method, which is where
  // AppKit's main-thread assertion bites hardest.
  LOG(INFO) << "dispatchCommand '" << commandName << "' on tag " << shadowView.tag
            << " is not implemented on macOS";
}

ComponentRegistryFactory MacMountingManager::getComponentRegistryFactory() {
  return facebook::react::getDefaultComponentRegistryFactory();
}

bool MacMountingManager::hasComponent(const std::string &name) {
  // Only what actually mounts. Claiming more would be worse than admitting the
  // gap: the registry would build shadow nodes nothing can put on screen, and
  // the app would render blank rectangles instead of failing somewhere legible.
  //
  // Text, Image, ScrollView and TextInput are all missing, and each is its own
  // piece of work -- Core Text, an image loader, a clipping scroller and an
  // NSTextField peer respectively. See plan/19-macos-mounting.md.
  return name == "View" || name == "RootView";
}

// ---------------------------------------------------------------------------
// What MountingWalk asks of a platform
// ---------------------------------------------------------------------------

RnMacView *MacMountingManager::createView(const ShadowView &shadowView) {
  return [RnMacView viewWithTag:static_cast<NSInteger>(shadowView.tag)];
}

RnMacView *MacMountingManager::createRootView(Tag tag) {
  return [RnMacView viewWithTag:static_cast<NSInteger>(tag)];
}

void MacMountingManager::destroyView(RnMacView *view) {
  // Nothing to do: ARC owns these. The registry holds the only strong reference
  // between a Remove and its Delete, exactly as on GTK, but erasing the entry
  // is what releases it rather than an explicit unref. This hook stays because
  // the walk has to say *when* a view stops being owned, even on a platform
  // where saying it costs nothing.
  (void)view;
}

void MacMountingManager::insertChild(RnMacView *parent, RnMacView *child, int index) {
  [parent insertRnChild:child atIndex:index];
}

void MacMountingManager::removeChild(RnMacView *parent, RnMacView *child) {
  [parent removeRnChild:child];
}

void MacMountingManager::forgetTag(Tag tag) {
  // No per-tag side tables yet. GTK has three -- image URIs, scroll views and
  // text inputs -- and each arrives here with the component it belongs to.
  (void)tag;
}

void MacMountingManager::updateView(RnMacView *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyLayoutMetrics(view, shadowView);
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a view
// ---------------------------------------------------------------------------

void MacMountingManager::applyProps(RnMacView *view, const ShadowView &shadowView) {
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

void MacMountingManager::applyLayoutMetrics(RnMacView *view, const ShadowView &shadowView) {
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

} // namespace rnlinux
