// The macOS mounting manager.
//
// `IMountingManager` is the one interface `ReactHost` requires in order to put
// a surface on screen. Almost none of what that takes is here: the mutation
// walk, the view registry, the event-emitter bookkeeping and the surface roots
// are in `core/MountingWalk.h`, written once and shared with GTK. What is left
// below is the part that is genuinely AppKit -- making a view, parenting it,
// and turning a ShadowView into layer state.
//
// That division is the claim this file exists to test. If it holds, a third
// desktop is a view layer and a few hundred lines rather than a second port of
// everything.

#pragma once

#include "MountingWalk.h"
#include "RnAppKitView.h"

#include <react/renderer/uimanager/IMountingManager.h>

#include <string>

namespace basalt {

class AppKitMountingManager final : public facebook::react::IMountingManager,
                                 public MountingWalk<AppKitMountingManager, RnAppKitView *> {
 public:
  AppKitMountingManager() = default;
  ~AppKitMountingManager() noexcept override;

  // --- IMountingManager -----------------------------------------------------

  // Called on the JS thread, from the event loop's "update the rendering" step.
  // Hands the transaction to the main queue; does not touch a view.
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
  // eventEmitterForTag all come from MountingWalk.

  // Applies a transaction to the view tree. Invoked on the main thread by
  // executeMount's marshalling -- not part of IMountingManager, and never to be
  // called directly. Public because the dispatch block has to reach it, and
  // because the tests call it rather than spinning a run loop.
  void applyTransaction(facebook::react::SurfaceId surfaceId,
                        facebook::react::MountingTransaction &&transaction);

 private:
  // --- What MountingWalk asks of a platform ---------------------------------
  friend class MountingWalk<AppKitMountingManager, RnAppKitView *>;

  RnAppKitView *createView(const facebook::react::ShadowView &shadowView);
  RnAppKitView *createRootView(facebook::react::Tag tag);
  void destroyView(RnAppKitView *view);
  void insertChild(RnAppKitView *parent, RnAppKitView *child, int index);
  void removeChild(RnAppKitView *parent, RnAppKitView *child);
  void updateView(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void forgetTag(facebook::react::Tag tag);

  // --- The AppKit half of updateView ----------------------------------------
  void applyProps(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyText(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
  void applyLayoutMetrics(RnAppKitView *view, const facebook::react::ShadowView &shadowView);
};

} // namespace basalt
