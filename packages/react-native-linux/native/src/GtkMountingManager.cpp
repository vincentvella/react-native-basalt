#include "GtkMountingManager.h"

#include "ComponentRegistry.h"
#include "PangoTextLayout.h"

#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/graphics/Transform.h>
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

// React Native's accessibilityRole is an open string, and its vocabulary is
// mostly ARIA's, which is also what GTK's roles are modelled on. Anything
// unrecognised falls back to GENERIC rather than guessing: a wrong role is
// worse for a screen reader than no role, because it makes the widget announce
// itself as something it is not.
GtkAccessibleRole toAccessibleRole(const std::string &role) {
  static const std::unordered_map<std::string, GtkAccessibleRole> kRoles = {
      {"button", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"togglebutton", GTK_ACCESSIBLE_ROLE_TOGGLE_BUTTON},
      {"link", GTK_ACCESSIBLE_ROLE_LINK},
      {"search", GTK_ACCESSIBLE_ROLE_SEARCH_BOX},
      {"image", GTK_ACCESSIBLE_ROLE_IMG},
      {"imagebutton", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"text", GTK_ACCESSIBLE_ROLE_LABEL},
      {"header", GTK_ACCESSIBLE_ROLE_ROW_HEADER},
      {"adjustable", GTK_ACCESSIBLE_ROLE_SLIDER},
      {"alert", GTK_ACCESSIBLE_ROLE_ALERT},
      {"checkbox", GTK_ACCESSIBLE_ROLE_CHECKBOX},
      {"combobox", GTK_ACCESSIBLE_ROLE_COMBO_BOX},
      {"menu", GTK_ACCESSIBLE_ROLE_MENU},
      {"menubar", GTK_ACCESSIBLE_ROLE_MENU_BAR},
      {"menuitem", GTK_ACCESSIBLE_ROLE_MENU_ITEM},
      {"progressbar", GTK_ACCESSIBLE_ROLE_PROGRESS_BAR},
      {"radio", GTK_ACCESSIBLE_ROLE_RADIO},
      {"radiogroup", GTK_ACCESSIBLE_ROLE_RADIO_GROUP},
      {"scrollbar", GTK_ACCESSIBLE_ROLE_SCROLLBAR},
      {"spinbutton", GTK_ACCESSIBLE_ROLE_SPIN_BUTTON},
      {"switch", GTK_ACCESSIBLE_ROLE_SWITCH},
      {"tab", GTK_ACCESSIBLE_ROLE_TAB},
      {"tablist", GTK_ACCESSIBLE_ROLE_TAB_LIST},
      {"list", GTK_ACCESSIBLE_ROLE_LIST},
      {"grid", GTK_ACCESSIBLE_ROLE_GRID},
      {"toolbar", GTK_ACCESSIBLE_ROLE_TOOLBAR},
      {"tooltip", GTK_ACCESSIBLE_ROLE_TOOLTIP},
      {"none", GTK_ACCESSIBLE_ROLE_PRESENTATION},
      {"presentation", GTK_ACCESSIBLE_ROLE_PRESENTATION},
  };

  const auto it = kRoles.find(role);
  return it == kRoles.end() ? GTK_ACCESSIBLE_ROLE_GENERIC : it->second;
}

RnAccessibleFlag toFlag(bool value) {
  return value ? RN_A11Y_TRUE : RN_A11Y_FALSE;
}

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
      textInputs_([this](Tag tag) { return eventEmitterForTag(tag); }),
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

// The same trip for an imperative command. See dispatchCommand below.
struct PendingCommand {
  GtkMountingManager *manager;
  Tag tag;
  std::string name;
  folly::dynamic args;
};

gboolean applyPendingCommand(gpointer data) {
  std::unique_ptr<PendingCommand> pending{static_cast<PendingCommand *>(data)};
  pending->manager->applyCommand(pending->tag, pending->name, pending->args);
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

        // The accessible role has to be decided now: GTK4 makes it
        // construct-only, and RnView is one class for every React Native view.
        GtkAccessibleRole role = GTK_ACCESSIBLE_ROLE_GENERIC;
        if (const auto accessibility =
                std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props)) {
          role = toAccessibleRole(accessibility->accessibilityRole);
        }
        if (role == GTK_ACCESSIBLE_ROLE_GENERIC && shadowView.componentName != nullptr) {
          // No explicit role, so infer one from the component. A <Text> is a
          // label and an <Image> is an image whether or not the app said so.
          const std::string_view name(shadowView.componentName);
          if (name == "Paragraph") {
            role = GTK_ACCESSIBLE_ROLE_LABEL;
          } else if (name == "Image") {
            role = GTK_ACCESSIBLE_ROLE_IMG;
          }
        }

        RnView *view = rn_view_new_with_role(static_cast<int>(shadowView.tag), role);

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
          textInputs_.remove(tag);
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
  // This arrives on the JS thread, inside the event loop's rendering update,
  // exactly like executeMount -- so it may not touch a widget either. That was
  // survivable while the only commands were ScrollView's, which only move an
  // adjustment; TextInput's `focus` reaches the platform input method, and on
  // macOS AppKit asserts it is on the main thread and traps the process.
  //
  // Queued at the same priority as a mount, so it stays behind the transaction
  // that created the view it names: g_idle sources at equal priority run in
  // the order they were added.
  auto *pending = new PendingCommand{this, shadowView.tag, commandName, args};
  g_idle_add_full(G_PRIORITY_DEFAULT, applyPendingCommand, pending, nullptr);
}

void GtkMountingManager::applyCommand(Tag tag,
                                      const std::string &commandName,
                                      const folly::dynamic &args) {
  assert(std::this_thread::get_id() == mainThreadId_ &&
         "applyCommand must run on the GTK main thread");

  if (scrollViews_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  if (textInputs_.dispatchCommand(tag, commandName, args)) {
    return;
  }
  g_debug("dispatchCommand '%s' on tag %d is not implemented",
          commandName.c_str(),
          static_cast<int>(tag));
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

// Accessibility, as AT-SPI and therefore Orca sees it.
//
// The role is not here: GTK4's accessible role is construct-only, so it is
// chosen when the widget is made. See the Create mutation.
void GtkMountingManager::applyAccessibility(RnView *view, const ShadowView &shadowView) {
  const auto props = std::dynamic_pointer_cast<const facebook::react::AccessibilityProps>(shadowView.props);
  if (props == nullptr) {
    return;
  }

  // A label given in props wins. Falling back to a Paragraph's own text means a
  // plain <Text> announces itself without the app having to repeat the string
  // in an accessibilityLabel.
  std::string label = props->accessibilityLabel;
  if (label.empty() && shadowView.componentName != nullptr &&
      std::string_view(shadowView.componentName) == "Paragraph") {
    if (const auto state =
            std::dynamic_pointer_cast<const facebook::react::ConcreteState<ParagraphState>>(shadowView.state)) {
      label = state->getData().attributedString.getString();
    }
  }

  rn_view_set_accessible_text(view, label.c_str(), props->accessibilityHint.c_str());

  if (props->accessibilityState.has_value()) {
    const auto &state = *props->accessibilityState;
    RnAccessibleFlag checked = RN_A11Y_UNSET;
    switch (state.checked) {
      case facebook::react::AccessibilityState::Checked:
        checked = RN_A11Y_TRUE;
        break;
      case facebook::react::AccessibilityState::Unchecked:
        checked = RN_A11Y_FALSE;
        break;
      case facebook::react::AccessibilityState::Mixed:
      case facebook::react::AccessibilityState::None:
        break;
    }
    rn_view_set_accessible_state(
        view,
        toFlag(state.disabled),
        checked,
        toFlag(state.selected),
        state.expanded.has_value() ? toFlag(*state.expanded) : RN_A11Y_UNSET,
        toFlag(state.busy));
  } else {
    rn_view_set_accessible_state(view, RN_A11Y_UNSET, RN_A11Y_UNSET, RN_A11Y_UNSET, RN_A11Y_UNSET, RN_A11Y_UNSET);
  }

  // accessibilityElementsHidden hides the subtree; importantForAccessibility
  // NoHideDescendants is Android's spelling of the same idea.
  const bool hidden = props->accessibilityElementsHidden ||
      props->importantForAccessibility == facebook::react::ImportantForAccessibility::NoHideDescendants;
  rn_view_set_accessible_hidden(view, hidden ? TRUE : FALSE);
}

void GtkMountingManager::applyTextInput(RnView *view, const ShadowView &shadowView) {
  if (shadowView.componentName == nullptr || std::string_view(shadowView.componentName) != "TextInput") {
    return;
  }
  textInputs_.update(view, shadowView);
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
      name == "ScrollView" || name == "TextInput";
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
  applyAccessibility(view, shadowView);
  applyTextInput(view, shadowView);
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

  rn_view_set_z_index(view, props->zIndex.value_or(0));

  // Radii and widths depend on the frame -- percentage radii, and the clamping
  // that stops opposite corners overlapping -- so they are resolved against the
  // layout metrics rather than read raw.
  const auto borders = props->resolveBorderMetrics(shadowView.layoutMetrics);

  // GRAPHENE_SIZE_INIT is a C99 compound literal, which C++ rejects here.
  const auto cornerSize = [](const auto &corner) {
    return graphene_size_t{static_cast<float>(corner.horizontal), static_cast<float>(corner.vertical)};
  };
  const graphene_size_t radii[4] = {
      cornerSize(borders.borderRadii.topLeft),
      cornerSize(borders.borderRadii.topRight),
      cornerSize(borders.borderRadii.bottomRight),
      cornerSize(borders.borderRadii.bottomLeft),
  };
  rn_view_set_border_radii(view, radii);

  // GTK's border node wants top, right, bottom, left -- the order CSS names
  // them in, and the order React Native's RectangleEdges is not stored in.
  const float widths[4] = {
      static_cast<float>(borders.borderWidths.top),
      static_cast<float>(borders.borderWidths.right),
      static_cast<float>(borders.borderWidths.bottom),
      static_cast<float>(borders.borderWidths.left),
  };
  const auto edgeColor = [](const facebook::react::SharedColor &color) {
    return color ? toRgba(colorComponentsFromColor(color)) : GdkRGBA{0.0F, 0.0F, 0.0F, 0.0F};
  };
  const GdkRGBA colors[4] = {
      edgeColor(borders.borderColors.top),
      edgeColor(borders.borderColors.right),
      edgeColor(borders.borderColors.bottom),
      edgeColor(borders.borderColors.left),
  };
  rn_view_set_borders(view, widths, colors);

  // resolveTransform folds in transformOrigin, but only when one was set: the
  // default anchor is the view's centre, and the widget layer applies that.
  const auto transform = props->resolveTransform(shadowView.layoutMetrics);
  if (transform == facebook::react::Transform::Identity()) {
    rn_view_set_transform(view, nullptr);
  } else {
    // React Native's matrix is CSS matrix3d order, which puts translation at
    // indices 12..14 -- the same slots graphene uses. Rotations are the
    // transpose of each other, which is why the demo checks a rotation on
    // screen rather than trusting the memory layout.
    float values[16];
    for (int i = 0; i < 16; i++) {
      values[i] = static_cast<float>(transform.matrix[static_cast<size_t>(i)]);
    }
    graphene_matrix_t matrix;
    graphene_matrix_init_from_float(&matrix, values);
    rn_view_set_transform(view, &matrix);
  }

  // TODO(props): borderStyles (dashed/dotted), pointerEvents, backfaceVisibility.
}

void GtkMountingManager::applyLayoutMetrics(RnView *view, const ShadowView &shadowView) {
  const auto &frame = shadowView.layoutMetrics.frame;
  rn_view_set_frame(view,
                    static_cast<float>(frame.origin.x),
                    static_cast<float>(frame.origin.y),
                    static_cast<float>(frame.size.width),
                    static_cast<float>(frame.size.height));

  // display: 'none' keeps the node in the shadow tree but takes it out of
  // layout and painting. gtk_widget_should_layout is false for an invisible
  // widget, so RnLayout skips it too.
  gtk_widget_set_visible(GTK_WIDGET(view),
                         shadowView.layoutMetrics.displayType != facebook::react::DisplayType::None);

  // TODO(layout): pointScaleFactor matters once fractional scaling is wired up.
}

} // namespace rnlinux
