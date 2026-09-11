// react-native-basalt — Fabric mounting layer
//
// Implements ReactCxxPlatform's IMountingManager: the single interface
// ReactHost requires in order to put a React Native surface on screen.
// Everything below translates ShadowViewMutation into GTK4 widget operations.

#pragma once

#include "GtkImageLoader.h"
#include "GtkScrollView.h"
#include "GtkTextInput.h"
#include "MountingWalk.h"
#include "RnView.h"

#include <react/renderer/uimanager/IMountingManager.h>

#include <memory>
#include <unordered_map>

namespace basalt {

// The mutation walk, the registry and the surface-root handling all live in
// MountingWalk and are shared with every other desktop platform. What is left
// here is the part that is genuinely GTK: making a widget, parenting it, and
// turning a ShadowView into widget state.
class GtkMountingManager final : public facebook::react::IMountingManager,
                                 public MountingWalk<GtkMountingManager, RnView *> {
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
  //
  // createSurfaceRoot, destroySurfaceRoot, getSurfaceRoot, viewForTag and
  // eventEmitterForTag come from MountingWalk. Callers cast the emitter to what
  // they need: GtkTouchDispatcher to TouchEventEmitter, the image path to
  // ImageEventEmitter.

  // Applies a transaction to the widget tree. Invoked on the GTK main thread by
  // executeMount's marshalling -- not part of IMountingManager, and never to be
  // called directly.
  // The main-thread half of dispatchCommand, public for the same reason
  // applyTransaction is: the idle callback that carries it across has to call
  // it, and the tests call it directly rather than spinning a main loop.
  void applyCommand(facebook::react::Tag tag,
                    const std::string &commandName,
                    const folly::dynamic &args);

  void applyTransaction(facebook::react::SurfaceId surfaceId,
                        facebook::react::MountingTransaction &&transaction);

 private:
  // --- What MountingWalk asks of a platform ---------------------------------
  friend class MountingWalk<GtkMountingManager, RnView *>;

  RnView *createView(const facebook::react::ShadowView &shadowView);
  RnView *createRootView(facebook::react::Tag tag);
  void destroyView(RnView *view);
  void insertChild(RnView *parent, RnView *child, int index);
  void removeChild(RnView *parent, RnView *child);
  void updateView(RnView *view, const facebook::react::ShadowView &shadowView);
  void forgetTag(facebook::react::Tag tag);

  // --- The GTK half of updateView -------------------------------------------
  void applyProps(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyText(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyImage(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyScrollView(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyAccessibility(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyTextInput(RnView *view, const facebook::react::ShadowView &shadowView);
  void applyLayoutMetrics(RnView *view, const facebook::react::ShadowView &shadowView);

  GtkImageLoader imageLoader_;
  GtkScrollViewManager scrollViews_;
  GtkTextInputManager textInputs_;

  // The source each <Image> is currently showing, so that a mutation which
  // changed only layout does not restart the load.
  std::unordered_map<facebook::react::Tag, std::string> imageUris_;
};

} // namespace basalt
