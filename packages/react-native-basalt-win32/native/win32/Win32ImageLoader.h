// The pixels behind an <Image>, fetched and decoded off the UI thread.
//
// React Native's cxx `ImageManager` is a stub -- `requestImage` returns an
// `ImageRequest` with no observer coordinator, so no `ImageResponse` ever
// arrives and `ImageState` never carries anything to render. The platform view
// is expected to load its own pixels, which is what Android does too, from
// `ReactImageView` rather than from the shadow node. `docs/DECISIONS.md` records
// the same conclusion for GTK.
//
// Two rules fall out of the lifetimes, and both are the reason this is a class
// rather than a function.
//
// **A load must survive the view that asked for it.** A view can be deleted
// while its bytes are still in flight, so the completion may not capture a
// view; the mounting manager looks the tag up again when it runs. That is the
// same arrangement GTK settled on.
//
// **A load must survive the loader.** The worker thread and the queued
// completion both outlive any particular call, and a mounting manager can be
// destroyed between the two -- which happens on surface teardown, not only at
// exit. So the cache lives in a shared state object that the in-flight work
// holds a reference to, and a completion arriving after the loader is gone
// finds the state alive and the callback already neutered. The AppKit loader
// captures its `this` raw and has the bug this avoids.

#pragma once

#include "RnWin32Image.h"

#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>

namespace basalt::win32 {

class Win32ImageLoader {
 public:
  // `image` is null when the load failed, and `error` says why. Always called
  // on the UI thread, and possibly before `load` has returned -- a cache hit is
  // answered synchronously, because making it asynchronous would mean an
  // <Image> that is already decoded still flickers through a frame of nothing.
  using Callback = std::function<void(std::shared_ptr<RnWin32Image> image,
                                      const std::string &error)>;

  Win32ImageLoader();
  ~Win32ImageLoader();

  Win32ImageLoader(const Win32ImageLoader &) = delete;
  Win32ImageLoader &operator=(const Win32ImageLoader &) = delete;

  void load(const std::string &uri, Callback callback);

  // Drops the decoded pixels. Nothing calls it yet, and that is a gap rather
  // than a decision: a long-lived app that scrolls through many remote images
  // grows without bound, which docs/BACKLOG.md records for GTK as well.
  void clearCache();

 private:
  struct State {
    // Locked, rather than relying on "everything happens on the UI thread".
    //
    // That reasoning was tried and is wrong here in a way worth recording,
    // because it looks right: `load` is called on the UI thread and every
    // completion is posted back to it, so a lock seems redundant. But
    // `postToUiThread` runs its work *inline* when no host has installed a
    // message loop, and inline on a worker thread is not the UI thread. The
    // load below avoids that case entirely by not spawning a worker when there
    // is nothing to marshal back to; this lock is the belt to that pair of
    // braces, and it costs a hash lookup's worth of nothing.
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<RnWin32Image>> cache;
    // Cleared when the loader is destroyed, so a completion that outlives it
    // delivers nowhere instead of into freed memory.
    bool alive = true;
  };

  std::shared_ptr<State> state_;
};

} // namespace basalt::win32
