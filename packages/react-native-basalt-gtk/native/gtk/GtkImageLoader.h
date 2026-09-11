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
// asks this to produce a texture.
//
// Scheme support:
//   file://       read from disk
//   http, https   fetched with libcurl, on a worker thread
//   data:         decoded inline
//   bare paths    treated as file paths, which is what a `require()`d asset
//                 looks like once Metro has resolved it in a release bundle
//
// Everything is decoded off the main thread and delivered back onto it, because
// GdkTexture is cheap to hand around but decoding a large PNG is not something
// to do in a mount transaction.

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace basalt {

class GtkImageLoader {
 public:
  // `texture` is null when the load failed, and `error` says why. Always called
  // on the GTK main thread, possibly synchronously if the image is cached.
  using Callback = std::function<void(GdkTexture *texture, const std::string &error)>;

  GtkImageLoader();
  ~GtkImageLoader();

  GtkImageLoader(const GtkImageLoader &) = delete;
  GtkImageLoader &operator=(const GtkImageLoader &) = delete;
  GtkImageLoader(GtkImageLoader &&) = delete;
  GtkImageLoader &operator=(GtkImageLoader &&) = delete;

  void load(const std::string &uri, Callback &&callback);

 private:
  struct Pending;

  static gboolean deliver(gpointer data);

  // Decoded textures, keyed by URI. Textures are immutable and shareable, so
  // two <Image>s with the same source paint the same object.
  //
  // Nothing evicts from this yet; see plan/backlog.md.
  std::unordered_map<std::string, GdkTexture *> cache_;
};

} // namespace basalt
