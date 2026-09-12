#include "Win32MountingManager.h"

#include "DirectWriteLayout.h"
#include "ImageBytes.h"
#include "PlatformServices.h"
#include "UIManagerAccess.h"

#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/text/ParagraphProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/uimanager/UIManager.h>

#include <memory>
#include <string_view>
#include <utility>

namespace basalt {

using facebook::react::ImageProps;
using facebook::react::ImageResizeMode;
using facebook::react::MountingTransaction;
using facebook::react::ParagraphState;
using facebook::react::ShadowView;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewProps;
using win32::RnImageFit;
using win32::RnWin32View;

Win32MountingManager::Win32MountingManager()
    : scrollViews_([this](Tag tag) { return eventEmitterForTag(tag); }),
      textInputs_([this](Tag tag) { return eventEmitterForTag(tag); }) {}

Win32MountingManager::~Win32MountingManager() noexcept {
  // MountingWalk cannot do this from its own destructor: by the time a base
  // destructor runs, the platform half that knows how to free a view is already
  // gone. Its header says so, and every platform has to make this call.
  releaseAllViews();
}

// ---------------------------------------------------------------------------
// IMountingManager
// ---------------------------------------------------------------------------

void Win32MountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // This runs on the JS thread: Scheduler::uiManagerDidFinishTransaction queues
  // the mount via RuntimeScheduler::scheduleRenderingUpdate, which drains in the
  // event loop's "update the rendering" step with the jsi::Runtime live. A view
  // here is not thread-affine the way a GtkWidget or an NSView is -- it is a
  // plain C++ object -- but the *painting* is, and so is every HWND a later
  // <TextInput> will own. So nothing here may touch a view either.
  //
  // Always post, never run inline even when already on the UI thread. The queue
  // is FIFO, and running one transaction inline while another is already
  // queued would reorder them -- so this is about ordering rather than about
  // thread-safety, and it is why the condition is absent rather than merely
  // unnecessary.
  //
  // That is this project's choice rather than everyone's, and worth being exact
  // about: iOS takes the other side. RCTMountingManager::scheduleTransaction
  // runs `initiateTransaction` inline when `RCTIsMainQueue()`, and only
  // dispatches otherwise. It gets away with it because under Fabric the JS
  // thread is not the main thread, so the fast path is reached only when
  // everything is already on one thread and there is nothing to reorder against.
  // GTK and AppKit both queue unconditionally here, and this matches them.
  //
  // None of this changed with the New Architecture. What JSI removed is the
  // *bridge* -- the JSON serialisation and the asynchronous message queue
  // between JavaScript and native, which is why a TurboModule call is now a
  // direct C++ call and why `measure` can answer synchronously. Mounting was
  // never on that path: Fabric computes layout off the UI thread, commits to
  // the shadow tree, and the mutations still have to be applied where the
  // platform permits. Thread affinity is USER32's rule, not the bridge's.
  //
  // MountingTransaction is move-only, so it travels in a shared_ptr the lambda
  // can capture -- std::function requires its target to be copyable.
  auto pending = std::make_shared<MountingTransaction>(std::move(transaction));
  postToUiThread([this, surfaceId, pending] {
    applyTransaction(surfaceId, std::move(*pending));
  });
}

void Win32MountingManager::applyTransaction(SurfaceId surfaceId,
                                            MountingTransaction &&transaction) {
  // The walk itself is in core/MountingWalk.h and is shared with the other two
  // desktops; what is Windows about mounting is below, in the operations it
  // calls back into.
  applyMutations(transaction.getMutations());

  // Tell the UIManager the transaction is on screen. Anything registered as a
  // mount hook -- Reanimated's is the one that matters -- is waiting for this,
  // and without it an animated style is computed every frame, committed to the
  // shadow tree, and never resumed, so nothing moves. ReactCxxPlatform calls it
  // nowhere; see plan/38-reanimated.md.
  if (auto uiManager = sharedUIManager()) {
    uiManager->reportMount(surfaceId);
  }

  // And ask the host to repaint, because on Windows nothing does that on its
  // own. See setOnDidMount.
  if (onDidMount_) {
    onDidMount_();
  }
}

void Win32MountingManager::setOnDidMount(std::function<void()> onDidMount) {
  onDidMount_ = std::move(onDidMount);
}

void Win32MountingManager::dispatchCommand(const ShadowView &shadowView,
                                           const std::string &commandName,
                                           const folly::dynamic &args) {
  // Same trip, for the same reason, and posted onto the same queue so it stays
  // behind the transaction that created the view it names. A scrollTo that
  // arrived before its ScrollView was mounted would find no entry and be
  // dropped.
  const Tag tag = shadowView.tag;
  postToUiThread([this, tag, commandName, args] { applyCommand(tag, commandName, args); });
}

void Win32MountingManager::applyCommand(Tag tag,
                                        const std::string &commandName,
                                        const folly::dynamic &args) {
  if (scrollViews_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  if (textInputs_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  // Dropped silently rather than logged. Every command the other two desktops
  // implement is now implemented here, so what reaches this line is a command
  // for a component this platform does not claim -- which is expected rather
  // than exceptional, and would otherwise be logged on every frame of a list.
}

facebook::react::ComponentRegistryFactory Win32MountingManager::getComponentRegistryFactory() {
  return facebook::react::getDefaultComponentRegistryFactory();
}

bool Win32MountingManager::hasComponent(const std::string &name) {
  // Only what actually mounts. Claiming more would be worse than admitting the
  // gap: the registry would build shadow nodes nothing can put on screen, and
  // the app would render blank rectangles instead of failing somewhere legible.
  //
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString.
  //
  // This list and ComponentRegistryWin32.cpp are two statements of one fact and
  // must agree.
  return name == "View" || name == "RootView" || name == "Paragraph" ||
      name == "ScrollView" || name == "Image" || name == "TextInput";
}

void Win32MountingManager::setUIManager(
    std::weak_ptr<facebook::react::UIManager> uiManager) noexcept {
  setSharedUIManager(std::move(uiManager));
}

// ---------------------------------------------------------------------------
// What MountingWalk asks of a platform
// ---------------------------------------------------------------------------

RnWin32View *Win32MountingManager::createView(const ShadowView &shadowView) {
  return new RnWin32View(shadowView.tag);
}

RnWin32View *Win32MountingManager::createRootView(Tag tag) {
  return new RnWin32View(tag);
}

void Win32MountingManager::destroyView(RnWin32View *view) {
  // The one operation that is genuinely different here. On GTK the registry
  // holds a g_object_ref_sink and this is an unref; under ARC it is nothing at
  // all, because erasing the map entry releases the last strong reference. A
  // plain C++ object has neither, so the registry's ownership is written out:
  // this is where a view stops existing.
  //
  // Safe against a view that is still parented, because ~RnWin32View detaches
  // itself -- though Fabric guarantees a Remove before every Delete, so that
  // should never be the path taken.
  delete view;
}

void Win32MountingManager::insertChild(RnWin32View *parent, RnWin32View *child, int index) {
  parent->insertChild(child, index);
}

void Win32MountingManager::removeChild(RnWin32View *parent, RnWin32View *child) {
  parent->removeChild(child);
}

void Win32MountingManager::forgetTag(Tag tag) {
  scrollViews_.remove(tag);
  textInputs_.remove(tag);
  imageUris_.erase(tag);
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a view
// ---------------------------------------------------------------------------

void Win32MountingManager::updateView(RnWin32View *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyImage(view, shadowView);
  applyAccessibility(view, shadowView);
  applyLayoutMetrics(view, shadowView);
  // Last: the scroll manager clamps its offset against the frame that was just
  // applied, and forces the clip that applyProps may have read as `visible`.
  applyScrollView(view, shadowView);
  applyTextInput(view, shadowView);
}

void Win32MountingManager::applyProps(RnWin32View *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const ViewProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  if (props->backgroundColor) {
    const auto components = facebook::react::colorComponentsFromColor(props->backgroundColor);
    view->setBackgroundColor(
        components.red, components.green, components.blue, components.alpha, true);
  } else {
    // Not transparent black: a view with no background does not paint at all.
    view->setBackgroundColor(0, 0, 0, 0, false);
  }

  view->setOpacity(props->opacity);

  // overflow: 'hidden'. React Native's default is 'visible'.
  view->setClipsChildren(props->getClipsContentToBounds());

  // Radii depend on the frame -- percentage radii, and the clamping that stops
  // opposite corners overlapping -- so they are resolved against the layout
  // metrics rather than read raw.
  const auto borders = props->resolveBorderMetrics(shadowView.layoutMetrics);
  // One radius where React Native has four, each with its own horizontal and
  // vertical value. Right for the overwhelmingly common single `borderRadius`
  // and visibly wrong for anything else; doing it properly needs a path
  // geometry rather than a rounded rect. The AppKit side has exactly the same
  // limitation for the same reason.
  view->setCornerRadius(borders.borderRadii.topLeft.horizontal);

  view->setZIndex(static_cast<int>(props->zIndex.value_or(0)));

  // resolveTransform folds in transformOrigin, but only when one was set: the
  // default anchor is the view's centre, which is what RnWin32View::paint
  // already assumes.
  const auto transform = props->resolveTransform(shadowView.layoutMetrics);
  if (transform == facebook::react::Transform::Identity()) {
    view->setTransform(nullptr);
  } else {
    view->setTransform(transform.matrix.data());
  }

  // TODO(props): per-corner radii, borders, pointerEvents. The GTK side has all
  // of them.
}

// A <Paragraph> carries its text in state, not props: ParagraphShadowNode
// resolves the whole <Text> subtree into one AttributedString and commits it as
// ParagraphState, which is why nothing here walks child shadow nodes.
void Win32MountingManager::applyText(RnWin32View *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Paragraph") {
    return;
  }

  const auto state =
      std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(
          shadowView.state);
  if (state == nullptr) {
    return;
  }

  const auto &data = state->getData();
  // Built through the same function the measurement seam uses, which is what
  // makes the painted lines break where the measured ones did.
  view->setTextLayout(
      win32::buildTextLayout(data.attributedString, data.paragraphAttributes));
}

namespace {

RnImageFit toImageFit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Contain:
      return RnImageFit::Contain;
    case ImageResizeMode::Stretch:
      return RnImageFit::Stretch;
    case ImageResizeMode::Center:
    case ImageResizeMode::None:
      return RnImageFit::Center;
    case ImageResizeMode::Repeat:
      // No tiled draw yet; centring is the least wrong single draw, and it is
      // what GTK falls back to as well.
      return RnImageFit::Center;
    case ImageResizeMode::Cover:
      break;
  }
  return RnImageFit::Cover;
}

} // namespace

void Win32MountingManager::applyImage(RnWin32View *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Image") {
    return;
  }

  const auto props = std::dynamic_pointer_cast<const ImageProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  const RnImageFit fit = toImageFit(props->resizeMode);
  const std::string uri =
      props->sources.empty() ? std::string{} : props->sources.front().uri;

  // A mutation that changed only layout must not restart the load, or an
  // <Image> flickers whenever its parent resizes. The fit is applied every time
  // regardless, because changing it is cheap and does not touch the pixels.
  const auto previous = imageUris_.find(shadowView.tag);
  if (previous != imageUris_.end() && previous->second == uri) {
    view->setImage(view->image(), fit);
    return;
  }
  imageUris_[shadowView.tag] = uri;

  if (uri.empty()) {
    view->setImage(nullptr, fit);
    return;
  }

  // Clear whatever was showing: the source changed, and leaving the old picture
  // up while the new one loads is worse than a blank box, because it looks like
  // the change did not take.
  view->setImage(nullptr, fit);

  // The fetch is in the shared half, which knows file, data: and http URIs --
  // a URI means the same thing on every desktop -- and the loader puts both it
  // and the decode on a worker thread.
  //
  // The completion captures the *tag*, not the view. A view can be deleted
  // while its bytes are in flight, and capturing the pointer would be a
  // use-after-free on a slow network; looking it up again is the arrangement
  // GTK settled on for the same reason. `this` is safe to capture because the
  // loader is a member and cannot outlive the manager -- and the loader itself
  // guards the case where the manager goes first.
  const Tag tag = shadowView.tag;
  imageLoader_.load(uri, [this, tag, uri, fit](std::shared_ptr<win32::RnWin32Image> image,
                                               const std::string &error) {
    (void)error;
    RnWin32View *target = viewForTag(tag);
    if (target == nullptr) {
      // Deleted while loading. Not an error, and not worth logging: scrolling a
      // list past an image faster than it arrives does exactly this.
      return;
    }
    // And the source may have changed *again* while this one was loading, in
    // which case a later load owns the view and this result is stale.
    const auto current = imageUris_.find(tag);
    if (current == imageUris_.end() || current->second != uri) {
      return;
    }
    target->setImage(std::move(image), fit);
  });
}

namespace {

win32::RnAccessibleFlag toFlag(bool value) {
  return value ? win32::RnAccessibleFlag::True : win32::RnAccessibleFlag::False;
}

// The role React Native effectively means for this view: what the app asked
// for, or what the component implies.
//
// A <Text> is a label and an <Image> is an image whether or not the app said
// so, which is what makes an ordinary screen navigable without every developer
// having annotated it. Both other platforms infer the same two.
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

void Win32MountingManager::applyAccessibility(RnWin32View *view, const ShadowView &shadowView) {
  const auto props =
      std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  win32::RnAccessibleInfo info;
  info.role = effectiveRole(shadowView);

  // A label given in props wins. Falling back to a Paragraph's own text means a
  // plain <Text> announces itself without the app having repeated the string in
  // an accessibilityLabel, which is what the other two hosts do.
  info.label = props->accessibilityLabel;
  if (info.label.empty() && view->textLayout() != nullptr) {
    info.label = view->textLayout()->text();
  }
  info.hint = props->accessibilityHint;

  const auto &state = props->accessibilityState;
  if (state.has_value()) {
    info.state.disabled = toFlag(state->disabled);
    info.state.selected = toFlag(state->selected);
    info.state.busy = toFlag(state->busy);
    if (state->checked != facebook::react::AccessibilityState::Unchecked ||
        !info.role.empty()) {
      // Tri-state on purpose: `checked` unset is not the same as false, and a
      // view that never mentions being checked must not report ToggleState_Off.
      // React Native's own enum has no "unset", so Unchecked on a view with no
      // role is treated as "did not say".
      info.state.checked = state->checked == facebook::react::AccessibilityState::Checked
          ? win32::RnAccessibleFlag::True
          : win32::RnAccessibleFlag::False;
    }
    if (state->expanded.has_value()) {
      info.state.expanded = toFlag(*state->expanded);
    }
  }

  // accessible={false} and accessibilityElementsHidden both mean "not for a
  // screen reader".
  info.hidden = !props->accessible ||
      props->importantForAccessibility == facebook::react::ImportantForAccessibility::NoHideDescendants;

  view->setAccessibleInfo(info);
}

void Win32MountingManager::applyLayoutMetrics(RnWin32View *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  view->setFrame(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);

  // display: 'none' keeps the node in the shadow tree but takes it out of
  // layout and painting -- and, here, out of hit testing too.
  view->setHidden(shadowView.layoutMetrics.displayType == facebook::react::DisplayType::None);

  // TODO(layout): pointScaleFactor, once a high-DPI backing store is involved.
}

void Win32MountingManager::applyScrollView(RnWin32View *view, const ShadowView &shadowView) {
  if (std::string_view(shadowView.componentName) != "ScrollView") {
    return;
  }
  scrollViews_.update(view, shadowView);
}

// ---------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------

bool Win32MountingManager::scrollAt(
    RnWin32View *root, double x, double y, double deltaX, double deltaY) {
  return scrollViews_.scrollAt(root, x, y, deltaX, deltaY);
}

// ---------------------------------------------------------------------------
// <TextInput>
// ---------------------------------------------------------------------------

void Win32MountingManager::applyTextInput(RnWin32View *view, const ShadowView &shadowView) {
  if (std::string_view(shadowView.componentName) != "TextInput") {
    return;
  }
  textInputs_.update(view, shadowView);
}

void Win32MountingManager::setHostWindow(HWND window) {
  textInputs_.setHostWindow(window);
}

void Win32MountingManager::syncTextInputBounds(RnWin32View *root) {
  textInputs_.syncBounds(root);
}

bool Win32MountingManager::handleControlCommand(WPARAM wparam, LPARAM lparam) {
  return textInputs_.handleControlCommand(wparam, lparam);
}

HBRUSH Win32MountingManager::controlColor(HDC deviceContext, HWND control) {
  return textInputs_.controlColor(deviceContext, control);
}

bool Win32MountingManager::typeIntoFocusedTextInput(const std::string &text) {
  return textInputs_.typeIntoFocused(text);
}

bool Win32MountingManager::focusTextInputAt(RnWin32View *root, double x, double y) {
  return textInputs_.focusAt(root, x, y);
}

} // namespace basalt
