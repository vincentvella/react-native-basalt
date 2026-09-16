// The mutation walk, with no toolkit in it.
//
// `IMountingManager` has two pure-virtual methods, and one of them --
// `executeMount` -- is where a platform is most likely to be wrong on its own.
// Fabric's mutation stream has rules that are not obvious from the interface:
// a Create does not attach, a Remove does not destroy, an Insert's index counts
// positions in the parent's *final* child list, virtual views have no peer at
// all, and a mutation naming a tag that no longer exists has to be survivable
// rather than fatal.
//
// None of that is about GTK or AppKit. Writing it twice means two chances to get
// it subtly different, and a difference here shows up as a layout that is wrong
// on one desktop and right on another -- which is the exact failure this project
// exists to avoid. So it is written once, here, and each platform supplies the
// half-dozen operations that do touch a view.
//
// CRTP rather than virtual dispatch: the view type differs per platform
// (`RnView *` under GObject, `RnAppKitView *` under ARC) and a template keeps it
// concrete in each translation unit, with no type erasure and no indirection on
// a path that runs for every mutation of every frame.
//
// A platform provides:
//
//   using ViewRef            = ...;       // a pointer, nullable
//   ViewRef createView(const ShadowView &);   // owned; props not yet applied
//   ViewRef createRootView(Tag);              // owned; a surface's root
//   void    destroyView(ViewRef);             // release the owned reference
//   void    insertChild(ViewRef parent, ViewRef child, int index);
//   void    removeChild(ViewRef parent, ViewRef child);
//   void    updateView(ViewRef, const ShadowView &);  // props, layout, state
//   void    forgetTag(Tag);                   // per-tag side tables, if any
//
// and must call `releaseAllViews()` from its own destructor: this base cannot,
// because by the time a base destructor runs the platform half is already gone.

#pragma once

#include "HoverTracker.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <glog/logging.h>

#include <cassert>
#include <thread>
#include <unordered_map>

namespace basalt {

template <typename Platform, typename ViewRefT>
class MountingWalk {
 public:
  using ViewRef = ViewRefT;
  using Tag = facebook::react::Tag;
  using SurfaceId = facebook::react::SurfaceId;
  using ShadowView = facebook::react::ShadowView;

  MountingWalk() : mainThreadId_(std::this_thread::get_id()) {}

  // The mounted view for a tag, or null. Not `viewForTag` on purpose: platform
  // code reads better saying what kind of thing it wants back.
  ViewRef viewForTag(Tag tag) const {
    const auto it = registry_.find(tag);
    return it == registry_.end() ? ViewRef{} : it->second;
  }

  // The event emitter for a mounted view, or null.
  //
  // Kept per tag rather than on the view because a view holds no React Native
  // types, and because a view can sit detached between a Remove and its Delete
  // while still needing to deliver a cancel.
  facebook::react::EventEmitter::Shared eventEmitterForTag(Tag tag) const {
    const auto it = eventEmitters_.find(tag);
    return it == eventEmitters_.end() ? nullptr : it->second;
  }

  // Which hover events this view listens for, as a HoverListener mask.
  //
  // Recorded here, next to the emitter, for the same reason: it comes off the
  // props on every Create and Update, and a touch dispatcher asking per motion
  // event must not have to reach into the shadow tree to find it. Views that
  // listen for none -- nearly all of them -- are not stored at all.
  std::uint16_t hoverListenersForTag(Tag tag) const {
    const auto it = hoverListeners_.find(tag);
    return it == hoverListeners_.end() ? HoverListenerNone : it->second;
  }

  // Fabric emits no Create for a surface's root: the root shadow node is the
  // base of every diff, so it must already exist when the first transaction
  // arrives. The host makes one before `ReactHost::startSurface` and parents it
  // into a window. In Fabric a SurfaceId *is* the root node's tag, which is what
  // lets the root take part in the registry like any other view.
  ViewRef createSurfaceRoot(SurfaceId surfaceId) {
    const Tag rootTag = static_cast<Tag>(surfaceId);
    if (const auto it = registry_.find(rootTag); it != registry_.end()) {
      return it->second;
    }
    ViewRef root = platform().createRootView(rootTag);
    registry_[rootTag] = root;
    return root;
  }

  void destroySurfaceRoot(SurfaceId surfaceId) {
    const Tag rootTag = static_cast<Tag>(surfaceId);
    if (const auto it = registry_.find(rootTag); it != registry_.end()) {
      platform().destroyView(it->second);
      registry_.erase(it);
    }
  }

  ViewRef getSurfaceRoot(SurfaceId surfaceId) const {
    return viewForTag(static_cast<Tag>(surfaceId));
  }

  // Applies one transaction's mutations, in order, on the platform's main
  // thread. Read off `StubViewTree::mutate`, React Native's own reference walk.
  void applyMutations(const facebook::react::ShadowViewMutationList &mutations) {
    assert(std::this_thread::get_id() == mainThreadId_ &&
           "mutations must be applied on the platform's main thread");

    for (const auto &mutation : mutations) {
      switch (mutation.type) {
        case facebook::react::ShadowViewMutation::Create:
          create(mutation.newChildShadowView);
          break;
        case facebook::react::ShadowViewMutation::Delete:
          destroy(mutation.oldChildShadowView.tag);
          break;
        case facebook::react::ShadowViewMutation::Insert:
          insert(mutation);
          break;
        case facebook::react::ShadowViewMutation::Remove:
          remove(mutation);
          break;
        case facebook::react::ShadowViewMutation::Update:
          update(mutation.newChildShadowView);
          break;
      }
    }
  }

 protected:
  // Not virtual and not public: this is a mixin, never a base pointer.
  ~MountingWalk() = default;

  // Platforms call this from their own destructor. See the note at the top.
  void releaseAllViews() {
    for (auto &[tag, view] : registry_) {
      (void)tag;
      platform().destroyView(view);
    }
    registry_.clear();
    eventEmitters_.clear();
    hoverListeners_.clear();
  }

  void rememberEventEmitter(const ShadowView &shadowView) {
    if (shadowView.eventEmitter != nullptr) {
      eventEmitters_[shadowView.tag] = shadowView.eventEmitter;
    }
    rememberHoverListeners(shadowView);
  }

  // An Update can take listeners away as well as add them, so a view that stops
  // listening is erased rather than left with its old mask.
  void rememberHoverListeners(const ShadowView &shadowView) {
    const auto *viewProps = dynamic_cast<const facebook::react::ViewProps *>(shadowView.props.get());
    const std::uint16_t mask =
        viewProps == nullptr ? HoverListenerNone : hoverListenersFrom(viewProps->events);
    if (mask == HoverListenerNone) {
      hoverListeners_.erase(shadowView.tag);
    } else {
      hoverListeners_[shadowView.tag] = mask;
    }
  }

  bool onMainThread() const {
    return std::this_thread::get_id() == mainThreadId_;
  }

 private:
  Platform &platform() {
    return static_cast<Platform &>(*this);
  }

  // Create allocates a view but does not attach it; an Insert follows, possibly
  // in a later transaction.
  void create(const ShadowView &shadowView) {
    ViewRef view = platform().createView(shadowView);
    platform().updateView(view, shadowView);
    registry_[shadowView.tag] = view;
    rememberEventEmitter(shadowView);
  }

  void destroy(Tag tag) {
    const auto it = registry_.find(tag);
    if (it == registry_.end()) {
      LOG(WARNING) << "Delete for unknown tag " << tag;
      return;
    }
    platform().destroyView(it->second);
    registry_.erase(it);
    eventEmitters_.erase(tag);
    hoverListeners_.erase(tag);
    platform().forgetTag(tag);
  }

  void insert(const facebook::react::ShadowViewMutation &mutation) {
    // Virtual views exist in the shadow tree only, to keep an EventEmitter
    // alive. They have no peer to parent.
    if (mutation.mutatedViewIsVirtual()) {
      return;
    }
    ViewRef parent = viewForTag(mutation.parentTag);
    ViewRef child = viewForTag(mutation.newChildShadowView.tag);
    if (parent == ViewRef{} || child == ViewRef{}) {
      // Survivable rather than fatal: this is what a bug upstream, or a
      // transaction racing a surface teardown, looks like from here.
      LOG(WARNING) << "Insert with unknown tag (parent " << mutation.parentTag << ", child "
                   << mutation.newChildShadowView.tag << ")";
      return;
    }
    // Props can change in the same transaction that inserts the view.
    platform().updateView(child, mutation.newChildShadowView);
    platform().insertChild(parent, child, static_cast<int>(mutation.index));
  }

  void remove(const facebook::react::ShadowViewMutation &mutation) {
    if (mutation.mutatedViewIsVirtual()) {
      return;
    }
    ViewRef parent = viewForTag(mutation.parentTag);
    ViewRef child = viewForTag(mutation.oldChildShadowView.tag);
    if (parent == ViewRef{} || child == ViewRef{}) {
      LOG(WARNING) << "Remove with unknown tag (parent " << mutation.parentTag << ", child "
                   << mutation.oldChildShadowView.tag << ")";
      return;
    }
    // Detached, not destroyed: the Delete is a separate mutation and may never
    // come -- a reparent is a Remove and an Insert with no Delete between them.
    platform().removeChild(parent, child);
  }

  void update(const ShadowView &shadowView) {
    ViewRef view = viewForTag(shadowView.tag);
    if (view == ViewRef{}) {
      LOG(WARNING) << "Update for unknown tag " << shadowView.tag;
      return;
    }
    platform().updateView(view, shadowView);
    // A clone carries a new emitter instance; keeping the old one would deliver
    // touches to a stale target.
    rememberEventEmitter(shadowView);
  }

  // Views are held with a strong reference from Create until Delete. Between a
  // Remove and its Delete a view has no parent, so this is the only thing
  // keeping it alive.
  std::unordered_map<Tag, ViewRef> registry_;

  // Parallel to registry_, and torn down with it on Delete.
  std::unordered_map<Tag, facebook::react::EventEmitter::Shared> eventEmitters_;

  // Sparse, unlike the two above: only views that listen for a hover event
  // appear, so an app that uses none carries an empty map.
  std::unordered_map<Tag, std::uint16_t> hoverListeners_;

  // The main thread, recorded at construction. `executeMount` arrives on the JS
  // thread and marshals here; `applyMutations` asserts it got there.
  std::thread::id mainThreadId_;
};

} // namespace basalt
