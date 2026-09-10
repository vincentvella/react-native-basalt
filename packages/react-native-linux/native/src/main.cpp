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
#include "GtkRunLoopObserver.h"
#include "GtkTouchDispatcher.h"
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
#include "ExpoRuntime.h"
#include "LinuxPlatformConstants.h"
#include "LinuxStatusBar.h"

#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>

#include <glib-unix.h>

#include <csignal>
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

// The entry point for the *scriptless* mode, where the bundle is a hand-written
// script talking to nativeFabricUIManager directly rather than a React app.
// See js/demo.js. Only used when no module name is given.
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
  std::unique_ptr<rnlinux::GtkTouchDispatcher> touchDispatcher;
  std::unique_ptr<ReactHost> reactHost;

  // Drives RunLoopObserverManager::onRender, without which no event an emitter
  // produces ever reaches JavaScript.
  GSource *runLoopObserver{nullptr};

  std::string bundlePath;
  // Empty means the bundle is a raw Fabric script rather than a React app; see
  // startSurface below for what that changes.
  std::string moduleName;
  // Metro's entry point, without the extension. Only meaningful in dev mode:
  // DevServerHelper builds "http://host:port/<sourcePath>.bundle?..." from it,
  // and returns an empty URL if it is unset, which silently falls back to the
  // on-disk bundle.
  std::string sourcePath;
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

// Same clean exit, on SIGINT or SIGTERM, so Ctrl-C and `kill` shut the runtime
// down instead of dropping it. The default disposition would kill the process
// where it stands, skipping GApplication::shutdown and everything under it --
// stopAllSurfaces, the teardown ordering, and the widget tree dump.
//
// g_unix_signal_add is the safe form: it does not run in the signal handler,
// it wakes the main loop and dispatches from there, so ordinary GLib calls are
// allowed here.
gboolean quitOnSignal(gpointer data) {
  auto *app = static_cast<GApplication *>(data);
  g_message("signal received; quitting");
  g_application_quit(app);
  return G_SOURCE_REMOVE;
}

// RN_LINUX_TEST_TAP: "x,y" pairs separated by ';', each fired a second apart.
// See GtkTouchDispatcher::synthesiseTap for why this exists.
struct PendingTap {
  Host *host;
  double x;
  double y;
};

gboolean fireTestTap(gpointer data) {
  auto *tap = static_cast<PendingTap *>(data);
  g_message("RN_LINUX_TEST_TAP: tapping (%.0f, %.0f)", tap->x, tap->y);
  if (tap->host->touchDispatcher != nullptr) {
    tap->host->touchDispatcher->synthesiseTap(tap->x, tap->y);
  }
  delete tap;
  return G_SOURCE_REMOVE;
}

// Returns the delay after the last tap, so typing can be scheduled behind it.
guint scheduleTestTaps(Host *host, const char *spec) {
  char **points = g_strsplit(spec, ";", -1);
  guint delayMs = 1500;
  for (char **point = points; *point != nullptr; ++point) {
    char **parts = g_strsplit(*point, ",", 2);
    if (parts[0] != nullptr && parts[1] != nullptr) {
      g_timeout_add(delayMs,
                    fireTestTap,
                    new PendingTap{host, g_ascii_strtod(parts[0], nullptr), g_ascii_strtod(parts[1], nullptr)});
      delayMs += 1000;
    }
    g_strfreev(parts);
  }
  g_strfreev(points);
  return delayMs;
}

// RN_LINUX_TEST_TYPE: text to insert into whatever field has focus, for the
// same reason RN_LINUX_TEST_TAP exists -- macOS cannot synthesise a real key
// event without accessibility permission an automated run does not have.
//
// It inserts through GtkEditable rather than through GDK, so it skips the key
// controller and the input method and exercises everything above them: the
// changed signal, the event emitter, React's re-render, and the controlled
// value coming back down. On Linux the end-to-end suite types with xdotool
// instead, which does go through GDK.
struct PendingType {
  Host *host;
  std::string text;
};

gboolean fireTestType(gpointer data) {
  std::unique_ptr<PendingType> pending{static_cast<PendingType *>(data)};
  g_message("RN_LINUX_TEST_TYPE: typing \"%s\"", pending->text.c_str());

  GtkWidget *focus = pending->host->window != nullptr
      ? gtk_window_get_focus(pending->host->window)
      : nullptr;
  if (focus == nullptr || !GTK_IS_TEXT(focus)) {
    g_warning("RN_LINUX_TEST_TYPE: no text field has focus");
    return G_SOURCE_REMOVE;
  }

  int position = -1;
  gtk_editable_insert_text(GTK_EDITABLE(focus),
                           pending->text.c_str(),
                           static_cast<int>(pending->text.size()),
                           &position);
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

// Feature flags this platform states explicitly. Anything not listed here keeps
// React Native's own default, which is what the base class provides.
class LinuxFeatureFlags : public facebook::react::ReactNativeFeatureFlagsDefaults {
 public:
  bool enableBridgelessArchitecture() override {
    return true;
  }
};

// TurboModules this platform supplies itself. ReactCxxTurboModuleProvider
// consults these before its own, so naming a module it also provides replaces
// it. Today that is only PlatformConstants; this is also the seam an Expo port
// would use.
facebook::react::TurboModuleProviders makeTurboModuleProviders() {
  facebook::react::TurboModuleProviders providers;
  providers.emplace_back(
      [](const std::string &name,
         const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == facebook::react::PlatformConstantsModule::kModuleName) {
          return std::make_shared<rnlinux::LinuxPlatformConstantsModule>(jsInvoker);
        }
        if (name == rnlinux::LinuxStatusBarModule::kModuleName) {
          return std::make_shared<rnlinux::LinuxStatusBarModule>(jsInvoker);
        }
        return nullptr;
      });
  return providers;
}

// Runs against the JavaScript runtime before the bundle is evaluated, which is
// the only moment early enough for what goes in here.
void installBindings(facebook::jsi::Runtime &runtime) {
  rnlinux::installExpoRuntime(runtime);
}

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

  // Before ReactHost, so the beat is being induced from the first event on.
  host->runLoopObserver = rnlinux::installRunLoopObserver(host->runLoopObserverManager);

  // Input. Attached to the root, which is where hit testing starts.
  host->touchDispatcher =
      std::make_unique<rnlinux::GtkTouchDispatcher>(host->mountingManager.get(), host->root);

  // Say what this host needs rather than inheriting a default that moves.
  //
  // HermesInstance gives the runtime a microtask queue only when
  // enableBridgelessArchitecture() is true, and React's scheduler enqueues a
  // microtask on its first render. That flag defaults to true on React Native
  // `main` and false on 0.87.1, so the same code that works against main throws
  // "Could not enqueue microtask because they are disabled in this runtime" on
  // a release, before anything appears on screen. This host is bridgeless --
  // there is no bridge here to be the alternative -- so it should have been
  // asserting that all along.
  //
  // iOS and Android override feature flags at startup too; relying on the
  // default was the anomaly.
  facebook::react::ReactNativeFeatureFlags::override(
      std::make_unique<LinuxFeatureFlags>());

  ReactInstanceConfig config;
  config.appId = "react-native-linux";
  config.deviceName = "linux";

  // Dev mode changes three things at once, which is worth being explicit about:
  // loadScript tries Metro before the on-disk bundle; DevServerHelper exists,
  // which is the only condition under which ReactCxxTurboModuleProvider serves
  // the DevSettings module a __DEV__ bundle requires; and ReactHost opens a
  // packager connection whose reload message reloads the instance.
  config.enableDevMode = g_getenv("RN_LINUX_DEV") != nullptr;
  config.enableInspector = config.enableDevMode;
  if (const char *devHost = g_getenv("RN_LINUX_DEV_HOST")) {
    config.devServerHost = devHost;
  }
  if (const char *devPort = g_getenv("RN_LINUX_DEV_PORT")) {
    config.devServerPort = static_cast<uint32_t>(g_ascii_strtoull(devPort, nullptr, 10));
  }
  if (config.enableDevMode) {
    g_message("dev mode: Metro at %s:%u, entry '%s'",
              config.devServerHost.c_str(),
              config.devServerPort,
              host->sourcePath.c_str());
  }

  try {
    host->reactHost = std::make_unique<ReactHost>(config,
                                                  host->mountingManager,
                                                  host->runLoopObserverManager,
                                                  makeContextContainer(),
                                                  facebook::react::getDefaultOnJsErrorFunc(),
                                                  logToGlib,
                                                  nullptr,
                                                  makeTurboModuleProviders(),
                                                  nullptr,
                                                  nullptr,
                                                  installBindings,
                                                  host->choreographer);
  } catch (const std::exception &error) {
    g_error("could not construct ReactHost: %s", error.what());
  }

  if (!host->reactHost->loadScript(host->bundlePath, host->sourcePath)) {
    g_warning("could not load script: %s", host->bundlePath.c_str());
    gtk_window_present(host->window);
    return;
  }
  g_message("loaded script: %s", host->bundlePath.c_str());

  // The module name decides who drives the surface.
  //
  // Non-empty: SurfaceHandler::start calls AppRegistry.runApplication, React
  // mounts the registered component, and everything after this point is
  // ordinary React Native.
  //
  // Empty: the surface is registered without JS being called at all, leaving it
  // for a script to commit into through nativeFabricUIManager by hand. That is
  // how this host ran before there was a Metro bundle, and js/demo.js still
  // exercises it.
  host->reactHost->startSurface(kSurfaceId,
                                host->moduleName,
                                folly::dynamic::object(),
                                constraintsFor(kInitialWidth, kInitialHeight),
                                layoutContextFor(host->scaleFactor));
  host->surfaceStarted = true;
  g_message("started surface %d%s%s",
            static_cast<int>(kSurfaceId),
            host->moduleName.empty() ? " (no module; raw Fabric script)" : " for module ",
            host->moduleName.c_str());

  gtk_window_present(host->window);

  if (host->moduleName.empty()) {
    g_message("--- committing tree 1 from JS ---");
    callRenderFunction(host, 1);
    g_timeout_add(2000, commitSecondTree, host);
  }

  guint scriptedDelayMs = 1500;
  if (const char *taps = g_getenv("RN_LINUX_TEST_TAP")) {
    scriptedDelayMs = scheduleTestTaps(host, taps);
  }
  if (const char *text = g_getenv("RN_LINUX_TEST_TYPE")) {
    g_timeout_add(scriptedDelayMs, fireTestType, new PendingType{host, text});
  }
  g_unix_signal_add(SIGINT, quitOnSignal, app);
  g_unix_signal_add(SIGTERM, quitOnSignal, app);

  if (const char *quitAfter = g_getenv("RN_LINUX_QUIT_AFTER_MS")) {
    const gint64 ms = g_ascii_strtoll(quitAfter, nullptr, 10);
    if (ms > 0) {
      g_timeout_add(static_cast<guint>(ms), quitAfterTimeout, app);
    }
  }
}

// RN_LINUX_DUMP_TREE: write the widget tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot. See docs/TESTING.md.
void dumpTreeIfRequested(Host *host) {
  const char *path = g_getenv("RN_LINUX_DUMP_TREE");
  if (path == nullptr || host->root == nullptr) {
    return;
  }
  char *description = rn_view_describe_tree(host->root);
  GError *error = nullptr;
  if (g_file_set_contents(path, description, -1, &error) == FALSE) {
    g_warning("could not write %s: %s", path, error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
  } else {
    g_message("wrote widget tree to %s", path);
  }
  g_free(description);
}

void onShutdown(GApplication * /*app*/, gpointer data) {
  auto *host = static_cast<Host *>(data);

  // Before the surface stops, which tears the tree down.
  dumpTreeIfRequested(host);

  if (host->choreographer != nullptr) {
    host->choreographer->detach();
  }
  // Stop feeding the beat before the manager it points at is released.
  rnlinux::removeRunLoopObserver(host->runLoopObserver);
  host->runLoopObserver = nullptr;
  host->touchDispatcher.reset();
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
  host.bundlePath = argc > 1 ? argv[1] : "build/main.jsbundle.js";
  host.moduleName = argc > 2 ? argv[2] : "RNLinuxDemo";
  const char *sourcePath = g_getenv("RN_LINUX_DEV_ENTRY");
  host.sourcePath = sourcePath != nullptr ? sourcePath : "index";

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
