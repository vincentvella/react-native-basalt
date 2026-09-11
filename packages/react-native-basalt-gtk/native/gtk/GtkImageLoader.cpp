#include "GtkImageLoader.h"

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace basalt {

namespace {

std::once_flag curlInitFlag;

size_t appendBytes(char *data, size_t size, size_t count, void *userData) {
  auto *out = static_cast<std::string *>(userData);
  out->append(data, size * count);
  return size * count;
}

bool startsWith(const std::string &value, const char *prefix) {
  return value.rfind(prefix, 0) == 0;
}

// Reads the bytes for a URI. Runs on a worker thread; returns false and sets
// `error` on failure.
bool fetchBytes(const std::string &uri, std::string *out, std::string *error) {
  if (startsWith(uri, "data:")) {
    const auto comma = uri.find(',');
    if (comma == std::string::npos) {
      *error = "malformed data: URI";
      return false;
    }
    const std::string meta = uri.substr(0, comma);
    const std::string payload = uri.substr(comma + 1);
    if (meta.find(";base64") == std::string::npos) {
      // Percent-encoded text payloads are legal but never carry an image.
      *error = "only base64 data: URIs are supported";
      return false;
    }
    gsize length = 0;
    guchar *decoded = g_base64_decode(payload.c_str(), &length);
    out->assign(reinterpret_cast<char *>(decoded), length);
    g_free(decoded);
    return true;
  }

  if (startsWith(uri, "http://") || startsWith(uri, "https://")) {
    std::call_once(curlInitFlag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL *handle = curl_easy_init();
    if (handle == nullptr) {
      *error = "could not initialise libcurl";
      return false;
    }
    curl_easy_setopt(handle, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBytes);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);

    if (result != CURLE_OK) {
      *error = curl_easy_strerror(result);
      return false;
    }
    if (status >= 400) {
      *error = "http " + std::to_string(status);
      return false;
    }
    return true;
  }

  // Anything else is a path. file:// is the explicit form; a bare path is what
  // a bundled asset looks like once it has been resolved.
  std::string path = uri;
  if (startsWith(uri, "file://")) {
    char *unescaped = g_filename_from_uri(uri.c_str(), nullptr, nullptr);
    if (unescaped == nullptr) {
      *error = "malformed file: URI";
      return false;
    }
    path = unescaped;
    g_free(unescaped);
  }

  gchar *contents = nullptr;
  gsize length = 0;
  GError *readError = nullptr;
  if (g_file_get_contents(path.c_str(), &contents, &length, &readError) == FALSE) {
    *error = readError != nullptr ? readError->message : "could not read file";
    g_clear_error(&readError);
    return false;
  }
  out->assign(contents, length);
  g_free(contents);
  return true;
}

} // namespace

// Carries one completed load from the worker thread to the main thread.
struct GtkImageLoader::Pending {
  GtkImageLoader *loader;
  std::string uri;
  std::string bytes;
  std::string error;
  Callback callback;
};

GtkImageLoader::GtkImageLoader() = default;

GtkImageLoader::~GtkImageLoader() {
  for (auto &[uri, texture] : cache_) {
    g_clear_object(&texture);
  }
  cache_.clear();
}

gboolean GtkImageLoader::deliver(gpointer data) {
  auto *pending = static_cast<Pending *>(data);

  GdkTexture *texture = nullptr;
  std::string error = pending->error;

  if (error.empty()) {
    // Decoding happens here rather than on the worker thread: GdkTexture is a
    // GObject, and creating one is only safe once, on the thread that will use
    // it. The expensive part -- the I/O -- already happened off-thread.
    GBytes *bytes = g_bytes_new(pending->bytes.data(), pending->bytes.size());
    GError *decodeError = nullptr;
    texture = gdk_texture_new_from_bytes(bytes, &decodeError);
    g_bytes_unref(bytes);
    if (texture == nullptr) {
      error = decodeError != nullptr ? decodeError->message : "could not decode image";
      g_clear_error(&decodeError);
    } else {
      pending->loader->cache_[pending->uri] = texture;
    }
  }

  pending->callback(texture, error);
  delete pending;
  return G_SOURCE_REMOVE;
}

void GtkImageLoader::load(const std::string &uri, Callback &&callback) {
  if (uri.empty()) {
    callback(nullptr, "empty source uri");
    return;
  }

  if (const auto it = cache_.find(uri); it != cache_.end()) {
    callback(it->second, {});
    return;
  }

  auto *pending = new Pending{this, uri, {}, {}, std::move(callback)};

  std::thread([pending]() {
    if (!fetchBytes(pending->uri, &pending->bytes, &pending->error)) {
      pending->bytes.clear();
    }
    g_idle_add_full(G_PRIORITY_DEFAULT, deliver, pending, nullptr);
  }).detach();
}

} // namespace basalt
