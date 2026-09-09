// The http and websocket seam for the Linux platform.
//
// `getHttpClientFactory()` and `getWebSocketClientFactory()` are declared by
// ReactCxxPlatform and deliberately left undefined there, exactly like
// `getDefaultComponentRegistryFactory()`: every host supplies its own. Fantom
// does the same, in tester/src/platform/oss/.
//
// ReactHost refuses to construct without both factories in its ContextContainer
// -- it throws "No HttpClientFactory provided" -- so a host cannot skip this
// file even when nothing it runs makes a request.
//
// Both implementations below are placeholders that fail politely. Nothing in
// the current milestone touches the network: the bundle is read from disk, dev
// mode and the inspector are off, and there is no Metro connection. They become
// real work in phase 3, where:
//
//   - websocket is what the packager connection and Fast Refresh ride on.
//     React Native already ships a working C++ client at
//     ReactCxxPlatform/react/http/platform/cxx/WebSocketClient.cpp (boost::beast
//     over OpenSSL). It is not in react_cxx_platform_react_http, whose
//     CMakeLists globs only its own directory, so wiring it up means adding
//     that source to the build rather than writing a client.
//   - http is what NetworkingModule (fetch/XHR) and remote images need.
//     libcurl or libsoup is the natural Linux backing; libsoup is already in
//     the GTK stack.

#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>

#include <glib.h>

#include <memory>
#include <string>

namespace facebook::react {

namespace {

class UnimplementedRequestToken final : public http::IRequestToken {
 public:
  void cancel() noexcept override {}
};

class UnimplementedHttpClient final : public IHttpClient {
 public:
  std::unique_ptr<http::IRequestToken> sendRequest(
      http::NetworkCallbacks && /*callbacks*/,
      const std::string &method,
      const std::string &url,
      const http::Headers & /*headers*/ = {},
      const http::Body & /*body*/ = {},
      uint32_t /*timeout*/ = 0,
      std::optional<std::string> /*loggingId*/ = std::nullopt) override {
    // Dropping the callbacks means the caller never hears back. That is
    // deliberate: a fabricated failure response would be indistinguishable from
    // a real server error and harder to diagnose than silence.
    g_warning("http is not implemented on this platform yet (%s %s)", method.c_str(), url.c_str());
    return std::make_unique<UnimplementedRequestToken>();
  }
};

class UnimplementedWebSocketClient final : public IWebSocketClient {
 public:
  void setOnClosedCallback(OnClosedCallback &&callback) noexcept override {
    onClosed_ = std::move(callback);
  }

  void setOnMessageCallback(OnMessageCallback &&callback) noexcept override {
    onMessage_ = std::move(callback);
  }

  void connect(const std::string &url, OnConnectCallback &&onConnect = nullptr) override {
    g_warning("websocket is not implemented on this platform yet (%s)", url.c_str());
    // Reporting the failure synchronously is what lets a caller move on rather
    // than wait for a connection that will never be established.
    if (onConnect) {
      onConnect(false, "websocket is not implemented on this platform yet");
    }
  }

  void close(const std::string &reason) override {
    if (onClosed_) {
      onClosed_(reason);
    }
  }

  void send(const std::string & /*message*/) override {}

  void ping() override {}

 private:
  OnClosedCallback onClosed_;
  OnMessageCallback onMessage_;
};

} // namespace

HttpClientFactory getHttpClientFactory() {
  return []() { return std::make_unique<UnimplementedHttpClient>(); };
}

WebSocketClientFactory getWebSocketClientFactory() {
  return []() { return std::make_unique<UnimplementedWebSocketClient>(); };
}

} // namespace facebook::react
