#import "AppKitMountingManager.h"

#import "CoreTextLayout.h"

#include "ComponentRegistry.h"
#include "ExpoImageComponent.h"
#include "UIManagerAccess.h"

#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/image/ImageProps.h>
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
using facebook::react::ImageEventEmitter;
using facebook::react::ImageProps;
using facebook::react::ImageResizeMode;
using facebook::react::ParagraphState;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewProps;

AppKitMountingManager::AppKitMountingManager()
    : scrollViews_([this](Tag tag) { return eventEmitterForTag(tag); }),
      textInputs_([this](Tag tag) { return eventEmitterForTag(tag); }) {}

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
  applyMutations(transaction.getMutations());

  // Tell the UIManager the transaction is on screen. Anything registered as a
  // mount hook -- Reanimated's is the one that matters here -- is waiting for
  // this, and without it an animated style is computed every frame, committed
  // to the shadow tree and never resumed, so nothing moves.
  //
  // iOS does this from RCTSurfacePresenter and Android from its mounting
  // manager. ReactCxxPlatform does it nowhere, which is a gap in the shared
  // platform rather than in either host: nothing in it had a mount hook until
  // a third-party library brought one.
  if (auto uiManager = sharedUIManager()) {
    uiManager->reportMount(surfaceId);
  }
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
  if (textInputs_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  LOG(INFO) << "dispatchCommand '" << commandName << "' on tag " << tag
            << " is not implemented on macOS";
}

void AppKitMountingManager::setUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) noexcept {
  setSharedUIManager(std::move(uiManager));
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
  // The four components an ordinary app is built from, plus the root. What is
  // left out is what neither desktop has: Switch, Modal, ActivityIndicator and
  // the rest. See plan/backlog.md.
  return name == "View" || name == "RootView" || name == "Paragraph" ||
      name == "ScrollView" || name == "Image" || name == "TextInput";
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
  textInputs_.remove(tag);
  imageUris_.erase(tag);
}

namespace {

RnAppKitImageFit toImageFit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Contain:
      return RnAppKitImageFitContain;
    case ImageResizeMode::Stretch:
      return RnAppKitImageFitStretch;
    case ImageResizeMode::Center:
    case ImageResizeMode::None:
      return RnAppKitImageFitCenter;
    case ImageResizeMode::Repeat:
      // No tiled draw yet; centring is the least wrong single draw.
      return RnAppKitImageFitCenter;
    case ImageResizeMode::Cover:
      break;
  }
  return RnAppKitImageFitCover;
}

} // namespace

// React Native's cxx ImageManager is a stub that never produces an
// ImageResponse, so nothing arrives through ImageState. The URI is read off the
// props and loaded here instead, which is also how Android does it.
void AppKitMountingManager::applyImage(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr) {
    return;
  }

  // Two components draw an image here: React Native's <Image> and expo-image's
  // view, which is an ordinary Fabric component with its own props (see
  // core/ExpoImageComponent.h). Everything past reading the source and the fit
  // off the props is identical, and the only other difference is which emitter
  // reports the result, because the two carry different payloads.
  const std::string_view componentName(shadowView.componentName);
  const bool isExpoImage = componentName == facebook::react::ExpoImageComponentName;
  if (componentName != "Image" && !isExpoImage) {
    return;
  }

  RnAppKitImageFit fit = RnAppKitImageFitCover;
  // Qualified: Carbon's headers put a CGImageSource in scope, and an
  // unqualified ImageSource resolves to that one.
  facebook::react::ImageSource source{};

  if (isExpoImage) {
    const auto props = std::dynamic_pointer_cast<const facebook::react::ExpoImageProps>(shadowView.props);
    if (props == nullptr) {
      return;
    }
    fit = toImageFit(props->contentFit);
    if (!props->sources.empty()) {
      source = props->sources.front();
    }
  } else {
    const auto props = std::dynamic_pointer_cast<const ImageProps>(shadowView.props);
    if (props == nullptr) {
      return;
    }
    fit = toImageFit(props->resizeMode);
    if (!props->sources.empty()) {
      source = props->sources.front();
    }
  }

  const std::string uri = source.uri;
  const Tag tag = shadowView.tag;

  // A mutation that changed only layout must not restart the load, or an
  // <Image> would flicker every time its parent resized. Re-requesting the same
  // URI is cheap -- the loader answers from its cache on this thread -- and it
  // reapplies the fit, which is the only thing that can have changed.
  const auto known = imageUris_.find(tag);
  if (known != imageUris_.end() && known->second == uri) {
    if (!uri.empty()) {
      imageLoader_.load(uri, [this, tag, fit](CGImageRef image, const std::string &) {
        if (RnAppKitView *target = viewForTag(tag); target != nil) {
          [target setRnImage:image fit:fit];
        }
      });
    }
    return;
  }

  imageUris_[tag] = uri;

  if (uri.empty()) {
    [view setRnImage:nullptr fit:fit];
    return;
  }

  // Always, rather than only when `shouldNotifyLoadEvents` is set.
  //
  // That prop is Android's signal, and it never arrives here.
  // `Image.android.js` sets it -- which is the Image.js this platform resolves
  // to -- but `ImageViewNativeComponent`'s view config branches on
  // `Platform.OS === 'android'`, and a platform that is neither takes the iOS
  // branch, whose `validAttributes` has no `shouldNotifyLoadEvents` in it. So
  // the prop is filtered out before it reaches C++ and onLoad/onError never
  // fire, silently, on both desktops.
  //
  // iOS does not use the prop at all: RCTImageComponentView emits load events
  // unconditionally and lets the emitter be the thing that knows whether
  // anybody is listening. Doing the same is both correct and cheaper than
  // forking a two-hundred-line view config to change one ternary -- and the
  // emitter lookup below already returns null when nothing is listening.
  if (isExpoImage) {
    if (auto emitter =
            std::dynamic_pointer_cast<const facebook::react::ExpoImageEventEmitter>(eventEmitterForTag(tag))) {
      emitter->onLoadStart();
    }
  } else if (auto emitter =
                 std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag))) {
    emitter->onLoadStart();
  }

  imageLoader_.load(uri, [this, tag, fit, source, isExpoImage](CGImageRef image,
                                                              const std::string &error) {
    // The view may have been deleted while the image was in flight, which is
    // why this looks the tag up again rather than capturing the view.
    if (RnAppKitView *target = viewForTag(tag); target != nil) {
      [target setRnImage:image fit:fit];
    }

    if (image == nullptr) {
      LOG(WARNING) << "image failed to load: " << source.uri << " (" << error << ")";
    }

    if (isExpoImage) {
      auto emitter =
          std::dynamic_pointer_cast<const facebook::react::ExpoImageEventEmitter>(eventEmitterForTag(tag));
      if (emitter == nullptr) {
        return;
      }
      if (image != nullptr) {
        // The pixel dimensions, which is what expo-image's onLoad reports and
        // what an app sizing itself to an image reads.
        emitter->onLoad(source,
                        static_cast<double>(CGImageGetWidth(image)),
                        static_cast<double>(CGImageGetHeight(image)));
      } else {
        emitter->onError(error);
      }
      return;
    }

    auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag));
    if (emitter == nullptr) {
      return;
    }
    if (image != nullptr) {
      emitter->onLoad(source);
    } else {
      emitter->onError(facebook::react::ImageErrorInfo{.error = error});
    }
    emitter->onLoadEnd();
  });
}

void AppKitMountingManager::updateView(RnAppKitView *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyImage(view, shadowView);
  // The peer first, then accessibility. A <TextInput>'s label belongs on its
  // peer and applyAccessibility can only put it there if the peer exists --
  // and on the mount that creates it, in this order it does. The GTK side
  // needed the same swap for the same reason.
  applyTextInput(view, shadowView);
  applyAccessibility(view, shadowView);
  applyLayoutMetrics(view, shadowView);
  // Last: the scroll manager clamps its offset against the frame it was just
  // given, and iOS documents the same ordering requirement -- layout before
  // state, or the offset is clamped against a stale size.
  applyScrollView(view, shadowView);
}

void AppKitMountingManager::applyTextInput(RnAppKitView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "TextInput") {
    return;
  }
  textInputs_.update(view, shadowView);
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

  // All four corners, each with its own horizontal and vertical radius. The
  // view layer keeps a single circular set on CALayer's own cornerRadius and
  // puts anything else on a mask layer; see setRnBorderRadii:.
  const CGFloat radii[8] = {
      (CGFloat)borders.borderRadii.topLeft.horizontal,
      (CGFloat)borders.borderRadii.topLeft.vertical,
      (CGFloat)borders.borderRadii.topRight.horizontal,
      (CGFloat)borders.borderRadii.topRight.vertical,
      (CGFloat)borders.borderRadii.bottomRight.horizontal,
      (CGFloat)borders.borderRadii.bottomRight.vertical,
      (CGFloat)borders.borderRadii.bottomLeft.horizontal,
      (CGFloat)borders.borderRadii.bottomLeft.vertical,
  };
  [view setRnBorderRadii:radii];

  // Widths and colours, top/right/bottom/left -- the order CSS names them and
  // the order the GTK side passes them in, so the two dumps line up.
  const CGFloat widths[4] = {
      (CGFloat)borders.borderWidths.top,
      (CGFloat)borders.borderWidths.right,
      (CGFloat)borders.borderWidths.bottom,
      (CGFloat)borders.borderWidths.left,
  };
  const auto edgeColor = [](const auto &color, CGFloat *out) {
    if (!color) {
      // Transparent, not black -- an edge with a width and no colour paints
      // nothing. The GTK side makes exactly the same call, and the two have to
      // agree or the dumps diverge on a view neither actually draws a border
      // for.
      out[0] = out[1] = out[2] = out[3] = 0;
      return;
    }
    const auto components = facebook::react::colorComponentsFromColor(*color);
    out[0] = (CGFloat)components.red;
    out[1] = (CGFloat)components.green;
    out[2] = (CGFloat)components.blue;
    out[3] = (CGFloat)components.alpha;
  };
  CGFloat colors[16];
  edgeColor(borders.borderColors.top, &colors[0]);
  edgeColor(borders.borderColors.right, &colors[4]);
  edgeColor(borders.borderColors.bottom, &colors[8]);
  edgeColor(borders.borderColors.left, &colors[12]);
  [view setRnBorderWidths:widths colors:colors];

  // resolveTransform folds in transformOrigin, but only when one was set: the
  // default anchor is the view's centre, which is what a layer-backed NSView
  // already uses.
  const auto transform = props->resolveTransform(shadowView.layoutMetrics);
  if (transform == facebook::react::Transform::Identity()) {
    [view setRnTransform:nullptr];
  } else {
    [view setRnTransform:transform.matrix.data()];
  }

  // Painting and hit testing only; see setRnZIndex:.
  [view setRnZIndex:(NSInteger)props->zIndex.value_or(0)];

  // Hit testing only. RnAppKitHitTest reads it; nothing about drawing does.
  switch (props->pointerEvents) {
    case facebook::react::PointerEventsMode::None:
      view.rnPointerEvents = RnAppKitPointerEventsNone;
      break;
    case facebook::react::PointerEventsMode::BoxNone:
      view.rnPointerEvents = RnAppKitPointerEventsBoxNone;
      break;
    case facebook::react::PointerEventsMode::BoxOnly:
      view.rnPointerEvents = RnAppKitPointerEventsBoxOnly;
      break;
    case facebook::react::PointerEventsMode::Auto:
      view.rnPointerEvents = RnAppKitPointerEventsAuto;
      break;
  }
}

namespace {

RnAppKitAccessibleFlag toFlag(bool value) {
  return value ? RnAppKitAccessibleTrue : RnAppKitAccessibleFalse;
}

// The role React Native effectively means for this view: what the app asked
// for, or what the component implies.
//
// A <Text> is a label and an <Image> is an image whether or not the app said
// so, which is what makes an ordinary screen navigable without every developer
// having annotated it. The GTK side infers the same two, at construction time,
// because its role is construct-only.
std::string effectiveRole(const ShadowView &shadowView) {
  if (const auto props =
          std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props)) {
    if (!props->accessibilityRole.empty()) {
      return props->accessibilityRole;
    }
  }
  if (shadowView.componentName != nullptr) {
    const std::string_view name(shadowView.componentName);
    if (name == "Paragraph") {
      return "text";
    }
    if (name == "Image") {
      return "image";
    }
  }
  return {};
}

} // namespace

// Accessibility, as VoiceOver sees it.
void AppKitMountingManager::applyAccessibility(RnAppKitView *view, const ShadowView &shadowView) {
  const auto props =
      std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  const std::string role = effectiveRole(shadowView);
  [view setRnAccessibleRole:role.empty() ? nil : [NSString stringWithUTF8String:role.c_str()]];

  // A label given in props wins. Falling back to a Paragraph's own text means a
  // plain <Text> announces itself without the app having to repeat the string
  // in an accessibilityLabel.
  std::string label = props->accessibilityLabel;
  if (label.empty() && shadowView.componentName != nullptr &&
      std::string_view(shadowView.componentName) == "Paragraph") {
    if (const auto state =
            std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(
                shadowView.state)) {
      label = state->getData().attributedString.getString();
    }
  }

  NSString *labelText = label.empty() ? nil : [NSString stringWithUTF8String:label.c_str()];
  NSString *hintText = props->accessibilityHint.empty()
      ? nil
      : [NSString stringWithUTF8String:props->accessibilityHint.c_str()];

  // A <TextInput>'s label belongs on its peer, not on this view.
  //
  // The peer is a real NSTextField and so is already an AXTextField in its own
  // right -- which is the element VoiceOver lands on. Left alone, the label
  // goes on the wrapper and the field announces itself as an unnamed "text
  // field", so a field the app carefully labelled is read out as if it had no
  // label at all. Checked with System Events: the group carried "your name"
  // and the AXTextField inside it carried nothing.
  //
  // The wrapper then stops being an element of its own, because two nested
  // elements for one control is a worse tree than one: a screen reader stops
  // twice and says the name once.
  if (NSView *peer = view.rnEditable) {
    peer.accessibilityLabel = labelText;
    if (hintText != nil) {
      peer.accessibilityHelp = hintText;
    }
    [view setRnAccessibleLabel:nil hint:nil];
    // Not `setRnAccessibleRole:@"none"`, which would be React Native's role
    // vocabulary and so would print in `describeTree` -- and GTK cannot answer
    // it, because a GtkAccessible role is construct-only. That made the two
    // hosts' trees disagree on this view and nothing else. Taking the wrapper
    // out of the accessibility tree directly says the same thing to VoiceOver
    // and nothing at all to the dump.
    view.accessibilityElement = NO;
    return;
  }

  [view setRnAccessibleLabel:labelText hint:hintText];

  if (props->accessibilityState.has_value()) {
    const auto &state = *props->accessibilityState;
    RnAppKitAccessibleFlag checked = RnAppKitAccessibleUnset;
    switch (state.checked) {
      case facebook::react::AccessibilityState::Checked:
        checked = RnAppKitAccessibleTrue;
        break;
      case facebook::react::AccessibilityState::Unchecked:
        checked = RnAppKitAccessibleFalse;
        break;
      case facebook::react::AccessibilityState::Mixed:
      case facebook::react::AccessibilityState::None:
        break;
    }
    [view setRnAccessibleStateDisabled:toFlag(state.disabled)
                               checked:checked
                              selected:toFlag(state.selected)
                              expanded:state.expanded.has_value()
                                           ? toFlag(*state.expanded)
                                           : RnAppKitAccessibleUnset
                                  busy:toFlag(state.busy)];
  } else {
    [view setRnAccessibleStateDisabled:RnAppKitAccessibleUnset
                               checked:RnAppKitAccessibleUnset
                              selected:RnAppKitAccessibleUnset
                              expanded:RnAppKitAccessibleUnset
                                  busy:RnAppKitAccessibleUnset];
  }

  // Keyboard focus. `accessible` is the signal because React Native's
  // `focusable` prop never reaches this platform -- see AppKitFocus.h -- and
  // because it is what <Pressable> sets on everything it renders. A hidden view
  // is not focusable whatever it says: Tab stopping on something nobody can see
  // is worse than Tab skipping it.
  view.rnFocusable = props->accessible && !props->accessibilityElementsHidden ? YES : NO;

  // accessibilityElementsHidden hides the subtree; importantForAccessibility
  // NoHideDescendants is Android's spelling of the same idea.
  const bool hidden = props->accessibilityElementsHidden ||
      props->importantForAccessibility ==
          facebook::react::ImportantForAccessibility::NoHideDescendants;
  [view setRnAccessibleHidden:hidden ? YES : NO];
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
