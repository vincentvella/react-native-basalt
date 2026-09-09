#include "GtkMountingManager.h"

#include "LinuxComponentRegistry.h"
#include "PangoTextLayout.h"

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
using facebook::react::ParagraphState;
using facebook::react::TouchEventEmitter;
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

GtkMountingManager::GtkMountingManager() : mainThreadId_(std::this_thread::get_id()) {}

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
                                         const folly::dynamic & /*args*/) {
  // TODO(commands): route to per-component handlers once ScrollView and
  // TextInput exist (scrollTo, focus, blur, ...).
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

bool GtkMountingManager::hasComponent(const std::string &name) {
  // Paragraph is the mountable half of <Text>; Text and RawText exist only in
  // the shadow tree, folded into the Paragraph's AttributedString. Image still
  // needs an IImageLoader, and ScrollView a GtkScrolledWindow peer.
  return name == "View" || name == "RootView" || name == "Paragraph";
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
  auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(shadowView.eventEmitter);
  if (emitter != nullptr) {
    eventEmitters_[shadowView.tag] = std::move(emitter);
  }
}

std::shared_ptr<const TouchEventEmitter> GtkMountingManager::eventEmitterForTag(Tag tag) const {
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
  applyLayoutMetrics(view, shadowView);
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

  // TODO(props): borderRadii, borderWidth/Colors, transform, overflow,
  // pointerEvents, accessibility. Each maps onto a GTK snapshot node or an
  // AT-SPI attribute.
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
