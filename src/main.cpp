// react-native-linux — the host process.
//
// Constructs a ReactHost, loads a script into Hermes, and runs one surface in
// a GtkWindow. Everything on screen from here on is produced by Fabric: the
// mutation stream arrives through GtkMountingManager exactly as it does on
// iOS and Android, and nothing hand-builds a ShadowViewMutation.
//
// The pieces a host has to supply, and where each one comes from:
//
//   IMountingManager        GtkMountingManager  (this repo)
//   RunLoopObserverManager  ReactCxxPlatform    (event beat)
//   AnimationChoreographer  GtkAnimationChoreographer (this repo, frame clock)
//   ContextContainer        http + websocket client factories, below
//   ComponentRegistryFactory LinuxComponentRegistry (via the mounting manager)
//
// Threading: GTK owns this thread. ReactHost spins up its own JS thread and
// every mount is marshalled back here by GtkMountingManager. Nothing below
// touches a widget off the main thread.

#include "GtkAnimationChoreographer.h"
#include "GtkMountingManager.h"
#include "RnView.h"

#include <jsi/jsi.h>
#include <logger/react_native_log.h>
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>
#include <react/logging/DefaultOnJsErrorHandler.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/runtime/ReactHost.h>
#include <react/runtime/ReactInstanceConfig.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <gtk/gtk.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

using facebook::react::ContextContainer;
using facebook::react::Float;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::LayoutDirection;
using facebook::react::ReactHost;
using facebook::react::ReactInstanceConfig;
using facebook::react::RunLoopObserverManager;
using facebook::react::Size;
using facebook::react::SurfaceId;

namespace {

// In Fabric a SurfaceId *is* the root shadow node's tag, which is what lets the
// surface root live in the mounting manager's registry like any other view.
constexpr SurfaceId kSurfaceId = 1;

constexpr int kInitialWidth = 900;
constexpr int kInitialHeight = 700;

// The JS entry point this host calls once the surface is running. The script is
// hand-written against nativeFabricUIManager -- there is no React, no
// AppRegistry and no Metro bundle yet, which is why the surface is started with
// an empty module name.
constexpr const char *kRenderFunctionName = "rnLinuxRender";

// ---------------------------------------------------------------------------
// Host state
// ---------------------------------------------------------------------------

struct Host {
  GtkWindow *window{nullptr};
  RnView *root{nullptr};

  std::shared_ptr<rnlinux::GtkMountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<rnlinux::GtkAnimationChoreographer> choreographer;
  std::unique_ptr<ReactHost> reactHost;

  std::string bundlePath;
  bool surfaceStarted{false};
  int scaleFactor{1};
};

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// ReactHost's Logger is where console.log and RN's own logging come out.
void logToGlib(const std::string &message, unsigned int logLevel) {
  switch (logLevel) {
    case ReactNativeLogLevelFatal:
    case ReactNativeLogLevelError:
      g_warning("[js] %s", message.c_str());
      break;
    case ReactNativeLogLevelWarning:
      g_message("[js warn] %s", message.c_str());
      break;
    default:
      g_message("[js] %s", message.c_str());
      break;
  }
}

// ---------------------------------------------------------------------------
// Layout constraints
// ---------------------------------------------------------------------------

// Minimum == maximum pins the root to the window's content box, which is what
// a full-window surface wants: the root fills, and flex children divide it.
// A surface that sized itself to its content would leave minimumSize at zero.
LayoutConstraints constraintsFor(int width, int height) {
  const Size size{.width = static_cast<Float>(width), .height = static_cast<Float>(height)};
  return LayoutConstraints{
      .minimumSize = size,
      .maximumSize = size,
      .layoutDirection = LayoutDirection::LeftToRight,
  };
}

LayoutContext layoutContextFor(int scaleFactor) {
  return LayoutContext{.pointScaleFactor = static_cast<Float>(scaleFactor)};
}

// ---------------------------------------------------------------------------
// Calling into JS
// ---------------------------------------------------------------------------

// Runs on the JS thread. `step` lets the script commit a second, different tree
// so an Update/Remove diff can be seen coming out of Fabric rather than only an
// initial mount.
void callRenderFunction(Host *host, int step) {
  if (host->reactHost == nullptr) {
    return;
  }
  host->reactHost->runOnRuntimeScheduler([step](facebook::jsi::Runtime &runtime) {
    try {
      auto value = runtime.global().getProperty(runtime, kRenderFunctionName);
      if (!value.isObject() || !value.getObject(runtime).isFunction(runtime)) {
        g_warning("script defines no global %s()", kRenderFunctionName);
        return;
      }
      value.getObject(runtime).getFunction(runtime).call(
          runtime,
          facebook::jsi::Value(static_cast<int>(kSurfaceId)),
          facebook::jsi::Value(step));
    } catch (const facebook::jsi::JSError &error) {
      g_warning("%s() threw: %s\n%s",
                kRenderFunctionName,
                error.getMessage().c_str(),
                error.getStack().c_str());
    } catch (const std::exception &error) {
      g_warning("%s() failed: %s", kRenderFunctionName, error.what());
    }
  });
}

// Quitting on a timer is the only way to exercise the shutdown path in
// automation: the window cannot be closed from a script, and killing the
// process skips GApplication::shutdown entirely, so stopAllSurfaces and the
// teardown ordering below it would never run under test. Opt in with
// RN_LINUX_QUIT_AFTER_MS.
gboolean quitAfterTimeout(gpointer data) {
  auto *app = static_cast<GApplication *>(data);
  g_message("RN_LINUX_QUIT_AFTER_MS elapsed; quitting");
  g_application_quit(app);
  return G_SOURCE_REMOVE;
}

gboolean commitSecondTree(gpointer data) {
  auto *host = static_cast<Host *>(data);
  g_message("--- committing tree 2 from JS ---");
  callRenderFunction(host, 2);
  return G_SOURCE_REMOVE;
}

// ---------------------------------------------------------------------------
// Window size -> surface constraints
// ---------------------------------------------------------------------------

void onRootResized(RnView * /*view*/, int width, int height, gpointer data) {
  auto *host = static_cast<Host *>(data);
  if (width <= 0 || height <= 0 || !host->surfaceStarted) {
    return;
  }
  g_debug("surface constraints -> %dx%d", width, height);
  host->reactHost->setSurfaceConstraints(
      kSurfaceId, constraintsFor(width, height), layoutContextFor(host->scaleFactor));
}

// The frame clock only exists once a widget is realised, so the choreographer
// cannot be attached at construction time.
void onRootMapped(GtkWidget *widget, gpointer data) {
  auto *host = static_cast<Host *>(data);
  if (host->choreographer != nullptr) {
    host->choreographer->attachToWidget(widget);
  }
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

std::shared_ptr<const ContextContainer> makeContextContainer() {
  auto contextContainer = std::make_shared<ContextContainer>();

  // ReactHost throws without these two. ReactCxxPlatform's own implementations
  // are real ones -- curl-backed http and a websocket client -- so a host that
  // is not a test harness should use them rather than stubs. The DevTools
  // variants default to the same pair when left unset.
  contextContainer->insert(facebook::react::HttpClientFactoryKey,
                           facebook::react::getHttpClientFactory());
  contextContainer->insert(facebook::react::WebSocketClientFactoryKey,
                           facebook::react::getWebSocketClientFactory());

  // MessageQueueThreadFactoryKey is deliberately left unset: ReactHost then
  // installs MessageQueueThreadImpl, a real threaded queue, which is what we
  // want. Fantom overrides it only to make test execution deterministic.
  return contextContainer;
}

void onActivate(GtkApplication *app, gpointer data) {
  auto *host = static_cast<Host *>(data);

  host->window = GTK_WINDOW(gtk_application_window_new(app));
  gtk_window_set_title(host->window, "react-native-linux");
  gtk_window_set_default_size(host->window, kInitialWidth, kInitialHeight);
  host->scaleFactor = gtk_widget_get_scale_factor(GTK_WIDGET(host->window));

  // Constructed here, on the GTK main thread: GtkMountingManager records this
  // thread and asserts that every widget mutation lands back on it.
  host->mountingManager = std::make_shared<rnlinux::GtkMountingManager>();

  // Fabric emits no Create for a surface root -- the root shadow node is the
  // base of every diff, so it has to exist before the surface starts.
  host->root = host->mountingManager->createSurfaceRoot(kSurfaceId);
  gtk_window_set_child(host->window, GTK_WIDGET(host->root));
  rn_view_set_resize_callback(host->root, onRootResized, host);
  g_signal_connect(host->root, "map", G_CALLBACK(onRootMapped), host);

  host->runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
  host->choreographer = std::make_shared<rnlinux::GtkAnimationChoreographer>();

  ReactInstanceConfig config;
  config.appId = "react-native-linux";
  config.deviceName = "linux";
  // No Metro yet: without this a debug build tries the dev server first and
  // only falls back to the bundle path after it fails to connect.
  config.enableDevMode = false;
  config.enableInspector = false;

  try {
    host->reactHost = std::make_unique<ReactHost>(config,
                                                  host->mountingManager,
                                                  host->runLoopObserverManager,
                                                  makeContextContainer(),
                                                  facebook::react::getDefaultOnJsErrorFunc(),
                                                  logToGlib,
                                                  nullptr,
                                                  facebook::react::TurboModuleProviders{},
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  host->choreographer);
  } catch (const std::exception &error) {
    g_error("could not construct ReactHost: %s", error.what());
  }

  if (!host->reactHost->loadScript(host->bundlePath, "")) {
    g_warning("could not load script: %s", host->bundlePath.c_str());
    gtk_window_present(host->window);
    return;
  }
  g_message("loaded script: %s", host->bundlePath.c_str());

  // An empty module name starts the surface without calling into
  // AppRegistry.runApplication: SurfaceHandler::start only reaches JS when a
  // module name is set. That is what lets a script with no React in it commit
  // into this surface.
  host->reactHost->startSurface(kSurfaceId,
                                "",
                                folly::dynamic::object(),
                                constraintsFor(kInitialWidth, kInitialHeight),
                                layoutContextFor(host->scaleFactor));
  host->surfaceStarted = true;
  g_message("started surface %d", static_cast<int>(kSurfaceId));

  gtk_window_present(host->window);

  g_message("--- committing tree 1 from JS ---");
  callRenderFunction(host, 1);
  g_timeout_add(2000, commitSecondTree, host);

  if (const char *quitAfter = g_getenv("RN_LINUX_QUIT_AFTER_MS")) {
    const gint64 ms = g_ascii_strtoll(quitAfter, nullptr, 10);
    if (ms > 0) {
      g_timeout_add(static_cast<guint>(ms), quitAfterTimeout, app);
    }
  }
}

void onShutdown(GApplication * /*app*/, gpointer data) {
  auto *host = static_cast<Host *>(data);

  if (host->choreographer != nullptr) {
    host->choreographer->detach();
  }
  if (host->reactHost != nullptr) {
    // Surfaces must stop before the host goes away, or teardown asserts.
    host->reactHost->stopAllSurfaces();
    host->surfaceStarted = false;
    // Destroying the host joins the JS thread, so it has to happen while the
    // mounting manager it holds is still alive.
    host->reactHost.reset();
  }
  host->choreographer.reset();
  host->runLoopObserverManager.reset();
  host->mountingManager.reset();
}

} // namespace

int main(int argc, char **argv) {
  Host host;
  host.bundlePath = argc > 1 ? argv[1] : "js/demo.js";

  // GTK would try to interpret argv as files to open. The bundle path is ours,
  // so the application never sees it.
  GtkApplication *app =
      gtk_application_new("dev.reactnativelinux.host", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(onActivate), &host);
  g_signal_connect(app, "shutdown", G_CALLBACK(onShutdown), &host);

  const int status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return status;
}
