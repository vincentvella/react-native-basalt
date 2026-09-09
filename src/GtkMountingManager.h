// react-native-linux — Fabric mounting layer
//
// Implements ReactCxxPlatform's IMountingManager: the single interface
// ReactHost requires in order to put a React Native surface on screen.
// Everything below translates ShadowViewMutation into GTK4 widget operations.

#pragma once

#include "GtkImageLoader.h"
#include "GtkScrollView.h"
#include "RnView.h"

#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/uimanager/IMountingManager.h>

#include <memory>
#include <thread>
#include <unordered_map>

namespace rnlinux {

class GtkMountingManager final : public facebook::react::IMountingManager {
 public:
  GtkMountingManager();
  ~GtkMountingManager() noexcept override;

  // --- IMountingManager -----------------------------------------------------

  // Called on the JS thread, from the event loop's "update the rendering"
  // step. Hands the transaction to the GTK main thread; does not touch widgets.
  void executeMount(facebook::react::SurfaceId surfaceId,
                    facebook::react::MountingTransaction &&transaction) override;

  void dispatchCommand(const facebook::react::ShadowView &shadowView,
                       const std::string &commandName,
                       const folly::dynamic &args) override;

  facebook::react::ComponentRegistryFactory getComponentRegistryFactory() override;

  bool hasComponent(const std::string &name) override;

  // --- Host-facing ----------------------------------------------------------

  // Fabric does not emit a Create mutation for a surface's root: the root
  // shadow node is the base of every diff, so it must already exist. The host
  // calls this before ReactHost::startSurface and parents the returned widget
  // into a GtkWindow. In Fabric a SurfaceId *is* the root node's tag, which is
  // what lets the root participate in the registry like any other view.
  RnView *createSurfaceRoot(facebook::react::SurfaceId surfaceId);
  void destroySurfaceRoot(facebook::react::SurfaceId surfaceId);
  RnView *getSurfaceRoot(facebook::react::SurfaceId surfaceId) const;

  // The event emitter for a mounted view, or null if the tag is unknown.
  // Callers cast to what they need: GtkTouchDispatcher to TouchEventEmitter,
  // the image path to ImageEventEmitter.
  //
  // Emitters are kept per tag rather than on the widget because RnView holds no
  // React Native types, and because a view can be detached between a Remove and
  // its Delete while still needing to deliver a cancel.
  facebook::react::EventEmitter::Shared eventEmitterForTag(facebook::react::Tag tag) const;

  // Applies a transaction to the widget tree. Invoked on the GTK main thread by
  // executeMount's marshalling -- not part of IMountingManager, and never to be
  // called directly.
  void applyTransaction(facebook::react::SurfaceId surfaceId,
                        facebook::react::MountingTransaction &&transaction);

 private:
  RnView *viewForTag(facebook::react::Tag tag) const;
  void rememberEventEmitter(const facebook::react::ShadowView &shadowView);

  void applyShadowView(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyProps(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyText(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyImage(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyScrollView(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyLayoutMetrics(RnView *view, const facebook::react::ShadowView &shadowView);

  // Views are held with a strong reference from Create until Delete. Between a
  // Remove and its Delete a view has no parent, so the registry is the only
  // thing keeping it alive.
  std::unordered_map<facebook::react::Tag, RnView *> registry_;

  // Parallel to registry_, and torn down with it on Delete.
  std::unordered_map<facebook::react::Tag, facebook::react::EventEmitter::Shared> eventEmitters_;

  GtkImageLoader imageLoader_;
  GtkScrollViewManager scrollViews_;

  // The source each <Image> is currently showing, so that a mutation which
  // changed only layout does not restart the load.
  std::unordered_map<facebook::react::Tag, std::string> imageUris_;

  // The GTK main thread, recorded at construction. executeMount arrives on the
  // JS thread and marshals here; applyTransaction asserts it got there.
  std::thread::id mainThreadId_;
};

} // namespace rnlinux
