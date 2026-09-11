// Loading the pixels behind <Image>.
//
// React Native's cxx platform does not do this for us. `ImageManager` has a
// platform variant under `imagemanager/platform/cxx`, and every method in it is
// a stub that returns an empty `ImageRequest` -- so no `ImageResponse` ever
// arrives, and `ImageState` never carries anything to render. The platform view
// is expected to load its own image, which is also what Android does (Fresco,
// from `ReactImageView`, not from the shadow node).
//
// So the mounting manager reads the URI straight off `ImageProps::sources` and
// asks this to produce a CGImage.
//
// Which URIs work is in core/ImageBytes.h, shared with the GTK loader. What is
// here is the decode and the cache, which is the only part that differs --
// and one thing that genuinely differs beyond spelling: a CGImage is immutable
// and thread-safe, so decoding happens on the worker thread. The GTK side has
// to decode on the main thread, because a GdkTexture is a GObject.

#pragma once

#import <Cocoa/Cocoa.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace basalt {

class AppKitImageLoader {
 public:
  // `image` is null when the load failed, and `error` says why. Always called
  // on the main thread, possibly synchronously if the image is cached.
  using Callback = std::function<void(CGImageRef image, const std::string &error)>;

  AppKitImageLoader();
  ~AppKitImageLoader();

  AppKitImageLoader(const AppKitImageLoader &) = delete;
  AppKitImageLoader &operator=(const AppKitImageLoader &) = delete;
  AppKitImageLoader(AppKitImageLoader &&) = delete;
  AppKitImageLoader &operator=(AppKitImageLoader &&) = delete;

  void load(const std::string &uri, Callback &&callback);

 private:
  struct Pending;

  // Decoded images, keyed by URI. CGImages are immutable and shareable, so two
  // <Image>s with the same source draw the same object.
  //
  // Nothing evicts from this yet; see plan/backlog.md.
  std::unordered_map<std::string, CGImageRef> cache_;
};

} // namespace basalt
