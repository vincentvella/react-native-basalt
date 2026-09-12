// The Windows mounting manager.
//
// `IMountingManager` is the one interface `ReactHost` requires in order to put
// a surface on screen. Almost none of what that takes is here: the mutation
// walk, the view registry, the event-emitter bookkeeping and the surface roots
// are in `core/MountingWalk.h`, written once and shared with GTK and AppKit.
// What is left below is the part that is genuinely Windows -- making a view,
// parenting it, and turning a ShadowView into view state.
//
// Phase 19 made that division and tested it with a second platform. This is the
// third, and it is the one that says whether the division holds or whether it
// was two platforms happening to agree: GTK and AppKit are both toolkits with a
// widget object per view, and this one is not.
//
// It holds. What is below is seven operations and a props translation, and the
// only place Windows differs in kind rather than in spelling is that
// `destroyView` has to actually delete -- there is no GObject refcount and no
// ARC, so the registry's `unique_ptr`-shaped ownership is written out.

#pragma once

#include "RnWin32Image.h"
#include "RnWin32View.h"
#include "Win32ImageLoader.h"

#include "MountingWalk.h"

#include <react/renderer/uimanager/IMountingManager.h>

#include <string>
#include <unordered_map>

namespace basalt {

class Win32MountingManager final : public facebook::react::IMountingManager,
                                   public MountingWalk<Win32MountingManager, win32::RnWin32View *> {
 public:
  Win32MountingManager();
  ~Win32MountingManager() noexcept override;

  // --- IMountingManager -----------------------------------------------------

  // Called on the JS thread, from the event loop's "update the rendering" step.
  // Hands the transaction to the UI thread; does not touch a view.
  void executeMount(facebook::react::SurfaceId surfaceId,
                    facebook::react::MountingTransaction &&transaction) override;

  void dispatchCommand(const facebook::react::ShadowView &shadowView,
                       const std::string &commandName,
                       const folly::dynamic &args) override;

  facebook::react::ComponentRegistryFactory getComponentRegistryFactory() override;

  bool hasComponent(const std::string &name) override;

  // ReactCxxPlatform hands the UIManager to the mounting manager and to nothing
  // else. Reanimated needs it, and is constructed from JavaScript where no host
  // object is in reach, so this passes it on. See core/UIManagerAccess.h.
  void setUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) noexcept override;

  // --- Host-facing ----------------------------------------------------------
  //
  // createSurfaceRoot, destroySurfaceRoot, getSurfaceRoot, viewForTag and
  // eventEmitterForTag all come from MountingWalk.

  // Applies a transaction to the view tree. Invoked on the UI thread by
  // executeMount's marshalling -- not part of IMountingManager, and never to be
  // called directly. Public because the posted work has to reach it, and
  // because the tests call it rather than pumping a message loop.
  void applyTransaction(facebook::react::SurfaceId surfaceId,
                        facebook::react::MountingTransaction &&transaction);

  // The UI-thread half of dispatchCommand, public for the same reason
  // applyTransaction is.
  void applyCommand(facebook::react::Tag tag,
                    const std::string &commandName,
                    const folly::dynamic &args);

 private:
  // --- What MountingWalk asks of a platform ---------------------------------
  friend class MountingWalk<Win32MountingManager, win32::RnWin32View *>;

  win32::RnWin32View *createView(const facebook::react::ShadowView &shadowView);
  win32::RnWin32View *createRootView(facebook::react::Tag tag);
  void destroyView(win32::RnWin32View *view);
  void insertChild(win32::RnWin32View *parent, win32::RnWin32View *child, int index);
  void removeChild(win32::RnWin32View *parent, win32::RnWin32View *child);
  void updateView(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);
  void forgetTag(facebook::react::Tag tag);

  // --- The Windows half of updateView ---------------------------------------
  void applyProps(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);
  void applyText(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);
  void applyImage(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);
  void applyAccessibility(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);
  void applyLayoutMetrics(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);

  // The source each <Image> is currently showing, so that a mutation which
  // changed only layout does not restart the load and make the image flicker
  // whenever its parent resizes. Both other platforms keep the same map.
  std::unordered_map<facebook::react::Tag, std::string> imageUris_;

  // Fetching and decoding, off the UI thread. Holds the decoded-pixel cache
  // too, and outlives this object while a load is in flight -- see the header.
  win32::Win32ImageLoader imageLoader_;
};

} // namespace basalt
