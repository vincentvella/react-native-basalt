#include "GtkMountingManager.h"

#include "LinuxComponentRegistry.h"
#include "PangoTextLayout.h"

#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/graphics/Color.h>

#include <cassert>
#include <type_traits>

namespace rnlinux {

// `override` proves each signature matches the interface, but not that every
// pure virtual is implemented -- nothing here instantiates the class. This
// catches an IMountingManager method going unimplemented as the interface
// evolves upstream.
static_assert(!std::is_abstract_v<GtkMountingManager>,
              "GtkMountingManager must implement all of IMountingManager");

using facebook::react::ColorComponents;
using facebook::react::ComponentRegistryFactory;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ImageEventEmitter;
using facebook::react::ImageProps;
using facebook::react::ImageResizeMode;
using facebook::react::ParagraphState;
using facebook::react::ViewProps;

namespace {

GdkRGBA toRgba(const ColorComponents &components) {
  GdkRGBA rgba;
  rgba.red = components.red;
  rgba.green = components.green;
  rgba.blue = components.blue;
  rgba.alpha = components.alpha;
  return rgba;
}

} // namespace

GtkMountingManager::GtkMountingManager()
    : scrollViews_([this](Tag tag) { return eventEmitterForTag(tag); }),
      mainThreadId_(std::this_thread::get_id()) {}

GtkMountingManager::~GtkMountingManager() noexcept {
  for (auto &[tag, view] : registry_) {
    g_object_unref(view);
  }
  registry_.clear();
}

// ---------------------------------------------------------------------------
// Mutation walk
// ---------------------------------------------------------------------------

namespace {

// Carries a transaction from the JS thread to the GTK main thread.
struct PendingMount {
  GtkMountingManager *manager;
  SurfaceId surfaceId;
  MountingTransaction transaction;
};

gboolean applyPendingMount(gpointer data) {
  auto *pending = static_cast<PendingMount *>(data);
  pending->manager->applyTransaction(pending->surfaceId, std::move(pending->transaction));
  delete pending;
  return G_SOURCE_REMOVE;
}

} // namespace

void GtkMountingManager::executeMount(SurfaceId surfaceId, MountingTransaction &&transaction) {
  // This runs on the JS thread: Scheduler::uiManagerDidFinishTransaction queues
  // the mount via RuntimeScheduler::scheduleRenderingUpdate, which drains in
  // the event loop's "update the rendering" step with the jsi::Runtime live.
  // GTK widgets are main-thread-only, so nothing here may touch them.
  //
  // Always queue, never invoke directly even when already on the main thread:
  // g_idle sources at equal priority run in the order they were added, which is
  // what keeps mutation ordering intact. iOS and Android marshal here too.
  auto *pending = new PendingMount{this, surfaceId, std::move(transaction)};
  g_idle_add_full(G_PRIORITY_DEFAULT, applyPendingMount, pending, nullptr);
}

void GtkMountingManager::applyTransaction(SurfaceId surfaceId, MountingTransaction &&transaction) {
  assert(std::this_thread::get_id() == mainThreadId_ &&
         "applyTransaction must run on the GTK main thread");

  for (const auto &mutation : transaction.getMutations()) {
    switch (mutation.type) {
      case ShadowViewMutation::Create: {
        // Create allocates a view but does not attach it; an Insert follows.
        const auto &shadowView = mutation.newChildShadowView;
        RnView *view = rn_view_new(static_cast<int>(shadowView.tag));

        // A fresh GtkWidget carries a floating reference. Sinking it here makes
        // the registry the owner, so the view survives the gap between a
        // Remove and the Insert that re-parents it.
        g_object_ref_sink(view);

        applyShadowView(view, shadowView);
        registry_[shadowView.tag] = view;
        rememberEventEmitter(shadowView);
        break;
      }

      case ShadowViewMutation::Delete: {
        const Tag tag = mutation.oldChildShadowView.tag;
        if (auto it = registry_.find(tag); it != registry_.end()) {
          g_object_unref(it->second);
          registry_.erase(it);
          eventEmitters_.erase(tag);
          imageUris_.erase(tag);
          scrollViews_.remove(tag);
        } else {
          g_warning("Delete for unknown tag %d", static_cast<int>(tag));
        }
        break;
      }

      case ShadowViewMutation::Insert: {
        if (mutation.mutatedViewIsVirtual()) {
          // Virtual views exist in the shadow tree only, to keep an
          // EventEmitter alive. They have no widget to parent.
          break;
        }
        RnView *parent = viewForTag(mutation.parentTag);
        RnView *child = viewForTag(mutation.newChildShadowView.tag);
        if (parent == nullptr || child == nullptr) {
          g_warning("Insert with unknown tag (parent %d, child %d)",
                    static_cast<int>(mutation.parentTag),
                    static_cast<int>(mutation.newChildShadowView.tag));
          break;
        }
        // Props can change in the same transaction that inserts the view.
        applyShadowView(child, mutation.newChildShadowView);
        rn_view_insert_child(parent, child, mutation.index);
        break;
      }

      case ShadowViewMutation::Remove: {
        if (mutation.mutatedViewIsVirtual()) {
          break;
        }
        RnView *parent = viewForTag(mutation.parentTag);
        RnView *child = viewForTag(mutation.oldChildShadowView.tag);
        if (parent == nullptr || child == nullptr) {
          g_warning("Remove with unknown tag (parent %d, child %d)",
                    static_cast<int>(mutation.parentTag),
                    static_cast<int>(mutation.oldChildShadowView.tag));
          break;
        }
        rn_view_remove_child(parent, child);
        break;
      }

      case ShadowViewMutation::Update: {
        const auto &shadowView = mutation.newChildShadowView;
        RnView *view = viewForTag(shadowView.tag);
        if (view == nullptr) {
          g_warning("Update for unknown tag %d", static_cast<int>(shadowView.tag));
          break;
        }
        applyShadowView(view, shadowView);
        // A clone carries a new emitter instance; keeping the old one would
        // deliver touches to a stale target.
        rememberEventEmitter(shadowView);
        break;
      }
    }
  }

  (void)surfaceId;
}

void GtkMountingManager::dispatchCommand(const ShadowView &shadowView,
                                         const std::string &commandName,
                                         const folly::dynamic &args) {
  if (scrollViews_.dispatchCommand(shadowView.tag, commandName, args)) {
    return;
  }
  // TODO(commands): focus/blur once TextInput exists.
  g_debug("dispatchCommand '%s' on tag %d is not implemented",
          commandName.c_str(),
          static_cast<int>(shadowView.tag));
}

ComponentRegistryFactory GtkMountingManager::getComponentRegistryFactory() {
  // The default factory registers the core components RN ships C++ descriptors
  // for (View, Text, Image, ScrollView, ...). Linux-specific components get
  // added here later.
  return facebook::react::getDefaultComponentRegistryFactory();
}

namespace {

RnImageFit toImageFit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Contain:
      return RN_IMAGE_FIT_CONTAIN;
    case ImageResizeMode::Stretch:
      return RN_IMAGE_FIT_STRETCH;
    case ImageResizeMode::Center:
    case ImageResizeMode::None:
      return RN_IMAGE_FIT_CENTER;
    case ImageResizeMode::Repeat:
      // No repeating draw yet; centring is the least wrong single draw.
      return RN_IMAGE_FIT_CENTER;
    case ImageResizeMode::Cover:
      break;
  }
  return RN_IMAGE_FIT_COVER;
}

} // namespace

// React Native's cxx ImageManager is a stub that never produces an
// ImageResponse, so nothing arrives through ImageState. The URI is read off the
// props and loaded here instead, which is also how Android does it.
void GtkMountingManager::applyImage(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr || std::string_view(shadowView.componentName) != "Image") {
    return;
  }

  const auto props = std::dynamic_pointer_cast<const ImageProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  const RnImageFit fit = toImageFit(props->resizeMode);
  const std::string uri = props->sources.empty() ? std::string{} : props->sources.front().uri;
  const Tag tag = shadowView.tag;

  // A mutation that changed only layout must not restart the load, or an
  // <Image> would flicker every time its parent resized. Re-requesting the same
  // URI is cheap -- the loader answers from its cache on this thread -- and it
  // reapplies the fit, which is the only thing that can have changed.
  const auto known = imageUris_.find(tag);
  if (known != imageUris_.end() && known->second == uri) {
    if (!uri.empty()) {
      imageLoader_.load(uri, [this, tag, fit](GdkTexture *texture, const std::string &) {
        if (RnView *target = viewForTag(tag); target != nullptr) {
          rn_view_set_texture(target, texture, fit);
        }
      });
    }
    return;
  }

  imageUris_[tag] = uri;

  if (uri.empty()) {
    rn_view_set_texture(view, nullptr, fit);
    return;
  }

  const bool notify = props->shouldNotifyLoadEvents;
  if (notify) {
    if (auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag))) {
      emitter->onLoadStart();
    }
  }

  const auto source = props->sources.front();
  imageLoader_.load(uri, [this, tag, fit, notify, source](GdkTexture *texture, const std::string &error) {
    // The view may have been deleted while the image was in flight, which is
    // why this looks the tag up again rather than capturing the widget.
    RnView *target = viewForTag(tag);
    if (target != nullptr) {
      rn_view_set_texture(target, texture, fit);
    }

    if (!notify) {
      if (texture == nullptr) {
        g_warning("image failed to load: %s (%s)", source.uri.c_str(), error.c_str());
      }
      return;
    }
    auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(eventEmitterForTag(tag));
    if (emitter == nullptr) {
      return;
    }
    if (texture != nullptr) {
      emitter->onLoad(source);
    } else {
      emitter->onError(facebook::react::ImageErrorInfo{.error = error});
    }
    emitter->onLoadEnd();
  });
}

void GtkMountingManager::applyScrollView(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr || std::string_view(shadowView.componentName) != "ScrollView") {
    return;
  }
  scrollViews_.update(view, shadowView);
}

bool GtkMountingManager::hasComponent(const std::string &name) {
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString. Image still
  // needs an IImageLoader, and ScrollView a GtkScrolledWindow peer.
  // ScrollView's content child arrives as "ScrollContentView", which the
  // registry rewrites to "View" before it reaches here, so it needs no entry.
  return name == "View" || name == "RootView" || name == "Paragraph" || name == "Image" ||
      name == "ScrollView";
}

// ---------------------------------------------------------------------------
// Surface roots
// ---------------------------------------------------------------------------

RnView *GtkMountingManager::createSurfaceRoot(SurfaceId surfaceId) {
  const Tag rootTag = static_cast<Tag>(surfaceId);

  if (auto it = registry_.find(rootTag); it != registry_.end()) {
    return it->second;
  }

  RnView *root = rn_view_new(static_cast<int>(rootTag));
  g_object_ref_sink(root);
  registry_[rootTag] = root;
  return root;
}

void GtkMountingManager::destroySurfaceRoot(SurfaceId surfaceId) {
  const Tag rootTag = static_cast<Tag>(surfaceId);
  if (auto it = registry_.find(rootTag); it != registry_.end()) {
    g_object_unref(it->second);
    registry_.erase(it);
  }
}

RnView *GtkMountingManager::getSurfaceRoot(SurfaceId surfaceId) const {
  return viewForTag(static_cast<Tag>(surfaceId));
}

// ---------------------------------------------------------------------------
// Applying a ShadowView to a widget
// ---------------------------------------------------------------------------

void GtkMountingManager::rememberEventEmitter(const ShadowView &shadowView) {
  if (shadowView.eventEmitter != nullptr) {
    eventEmitters_[shadowView.tag] = shadowView.eventEmitter;
  }
}

facebook::react::EventEmitter::Shared GtkMountingManager::eventEmitterForTag(Tag tag) const {
  const auto it = eventEmitters_.find(tag);
  return it == eventEmitters_.end() ? nullptr : it->second;
}

RnView *GtkMountingManager::viewForTag(Tag tag) const {
  const auto it = registry_.find(tag);
  return it == registry_.end() ? nullptr : it->second;
}

void GtkMountingManager::applyShadowView(RnView *view, const ShadowView &shadowView) {
  applyProps(view, shadowView);
  applyText(view, shadowView);
  applyImage(view, shadowView);
  applyLayoutMetrics(view, shadowView);
  // Last: the scroll manager clamps its offset against the frame it was just
  // given, and iOS documents the same ordering requirement -- layout before
  // state, or the offset is clamped against a stale size.
  applyScrollView(view, shadowView);
}

// A <Paragraph> carries its text in state, not props: ParagraphShadowNode
// resolves the whole <Text> subtree into one AttributedString and commits it as
// ParagraphState, which is why nothing here walks child shadow nodes.
void GtkMountingManager::applyText(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) != "Paragraph") {
    return;
  }

  const auto state = std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(shadowView.state);
  if (state == nullptr) {
    return;
  }

  const auto &data = state->getData();

  // Measurement already ran through the same builder, with the width Yoga then
  // assigned. Rebuilding it here at that width is what makes the painted lines
  // break where the measured ones did.
  const float width = static_cast<float>(shadowView.layoutMetrics.frame.size.width);
  PangoLayout *layout = rnlinux::buildTextLayout(data.attributedString, data.paragraphAttributes, width);

  // Fragments carry their own colours as Pango attributes; this is the fallback
  // for text that set none. React Native's default is opaque black.
  GdkRGBA color{0.0F, 0.0F, 0.0F, 1.0F};
  const auto &fragments = data.attributedString.getFragments();
  if (!fragments.empty() && fragments.front().textAttributes.foregroundColor) {
    color = toRgba(colorComponentsFromColor(fragments.front().textAttributes.foregroundColor));
  }

  rn_view_set_text_layout(view, layout, &color);
  g_object_unref(layout);
}

void GtkMountingManager::applyProps(RnView *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const ViewProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  if (props->backgroundColor) {
    const GdkRGBA rgba = toRgba(colorComponentsFromColor(props->backgroundColor));
    rn_view_set_background_color(view, TRUE, &rgba);
  } else {
    rn_view_set_background_color(view, FALSE, nullptr);
  }

  rn_view_set_opacity(view, props->opacity);

  // overflow: 'hidden'. React Native's default is 'visible', which is why the
  // phase-1 screenshots show a child outgrowing its shrunk parent.
  rn_view_set_clips_children(view, props->getClipsContentToBounds() ? TRUE : FALSE);

  // TODO(props): borderRadii, borderWidth/Colors, transform, pointerEvents,
  // accessibility. Each maps onto a GTK snapshot node or an AT-SPI attribute.
}

void GtkMountingManager::applyLayoutMetrics(RnView *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  rn_view_set_frame(view,
                    static_cast<float>(frame.origin.x),
                    static_cast<float>(frame.origin.y),
                    static_cast<float>(frame.size.width),
                    static_cast<float>(frame.size.height));

  // TODO(layout): displayType == DisplayType::None should hide the widget;
  // pointScaleFactor matters once fractional scaling is wired up.
}

} // namespace rnlinux
