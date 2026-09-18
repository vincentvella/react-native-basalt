#import "AppKitImageLoader.h"

#include "ImageBytes.h"

#import <ImageIO/ImageIO.h>

#include <thread>
#include <utility>

namespace basalt {

// Carries one completed load from the worker thread to the main thread.
struct AppKitImageLoader::Pending {
  AppKitImageLoader *loader;
  std::string uri;
  CGImageRef image;
  std::string error;
  Callback callback;
};

AppKitImageLoader::AppKitImageLoader() = default;

AppKitImageLoader::~AppKitImageLoader() {
  for (auto &[uri, image] : cache_) {
    (void)uri;
    CGImageRelease(image);
  }
  cache_.clear();
}

namespace {

// ImageIO rather than NSImage. An NSImage is a list of representations at
// different sizes with a resolution attached, and asking one for a CGImage
// means telling it a size and a context -- so a 160x100 PNG can come back
// 320x200 on a Retina display, which then measures wrong. CGImageSource hands
// back exactly what is in the file.
CGImageRef decode(const std::string &bytes, std::string *error) {
  if (bytes.empty()) {
    *error = "no image data";
    return nullptr;
  }

  CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                reinterpret_cast<const UInt8 *>(bytes.data()),
                                static_cast<CFIndex>(bytes.size()));
  if (data == nullptr) {
    *error = "could not wrap image data";
    return nullptr;
  }

  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CFRelease(data);
  if (source == nullptr) {
    *error = "unrecognised image format";
    return nullptr;
  }

  // Index 0: an animated GIF and a multi-page TIFF both have more, and drawing
  // the first frame is the honest thing to do until there is an animator. See
  // docs/BACKLOG.md.
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (image == nullptr) {
    *error = "could not decode image";
    return nullptr;
  }
  return image;
}

} // namespace

void AppKitImageLoader::load(const std::string &uri, Callback &&callback) {
  if (uri.empty()) {
    callback(nullptr, "empty source uri");
    return;
  }

  if (const auto it = cache_.find(uri); it != cache_.end()) {
    callback(it->second, {});
    return;
  }

  auto *pending = new Pending{this, uri, nullptr, {}, std::move(callback)};

  std::thread([pending]() {
    std::string bytes;
    // Both the fetch and the decode run here. A CGImage is immutable and
    // thread-safe, so unlike the GTK side there is nothing that has to wait for
    // the main thread except the delivery itself.
    if (fetchImageBytes(pending->uri, &bytes, &pending->error)) {
      pending->image = decode(bytes, &pending->error);
    }

    dispatch_async_f(dispatch_get_main_queue(), pending, [](void *data) {
      std::unique_ptr<Pending> done{static_cast<Pending *>(data)};
      if (done->image != nullptr) {
        // The cache takes the reference the decode produced; the callback
        // borrows it, and a view that keeps the image retains its own.
        done->loader->cache_[done->uri] = done->image;
      }
      done->callback(done->image, done->error);
    });
  }).detach();
}

} // namespace basalt
