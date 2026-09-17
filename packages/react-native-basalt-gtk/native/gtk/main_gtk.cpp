// react-native-basalt — the host process.
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
//   ComponentRegistryFactory ComponentRegistry (via the mounting manager)
//
// Threading: GTK owns this thread. ReactHost spins up its own JS thread and
// every mount is marshalled back here by GtkMountingManager. Nothing below
// touches a widget off the main thread.

#include "GtkAnimationChoreographer.h"
#include "AppIdentity.h"
#include "DevMenu.h"
#include "WindowControl.h"
#include "DialogModule.h"
#include "GtkMountingManager.h"
#include "GtkRunLoopObserver.h"
#include "GtkFocus.h"
#include "LogBoxSurface.h"

#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
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
#include "AppearanceModule.h"
#include "BlobModule.h"
#include "CoreModules.h"
#include "ColorScheme.h"
#include "DevBundle.h"
#include "GtkTitleBar.h"
#include "GtkWindowModule.h"
#include "ExpoModules.h"
#include "GestureHandlerModule.h"
#include "ReanimatedModule.h"
#include "UIManagerAccess.h"
#include "WorkletsModule.h"
#include "ExpoRuntime.h"
#include "PlatformConstantsModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"

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

// React Native's error inspector, which is a surface of its own -- registered
// by AppRegistry under the name "LogBox", exactly as an app registers its own
// component. Started over the app's when NativeLogBox.show() asks; see
// core/LogBoxSurface.h.
constexpr SurfaceId kLogBoxSurfaceId = 2;

constexpr int kInitialWidth = 900;
constexpr int kInitialHeight = 700;

// The entry point for the *scriptless* mode, where the bundle is a hand-written
// script talking to nativeFabricUIManager directly rather than a React app.
// See js/demo.js. Only used when no module name is given.
constexpr const char *kRenderFunctionName = "basaltRender";

// ---------------------------------------------------------------------------
// Host state
// ---------------------------------------------------------------------------

struct Host {
  GtkWindow *window{nullptr};
  RnView *root{nullptr};

  std::shared_ptr<basalt::GtkMountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<basalt::GtkAnimationChoreographer> choreographer;
  std::unique_ptr<basalt::GtkTouchDispatcher> touchDispatcher;
  std::unique_ptr<basalt::GtkFocusManager> focusManager;
  // The overlay the surface root sits in, and the error inspector's own root
  // when it is showing. The overlay was already here for the window controls;
  // the inspector goes above them, which is what a modal box should do.
  GtkWidget *overlay{nullptr};
  RnView *logBoxRoot{nullptr};
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
  // Whether this run is a development one, which is the only thing that
  // decides whether Ctrl+D opens anything. Kept because the ReactInstanceConfig
  // it came from is not.
  bool devMode{false};
  int scaleFactor{1};
  // Read by main() after the loop ends. GApplication has no exit status to set
  // any more, and a startup that failed has to be tellable from one that ran.
  int exitStatus{0};
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
// BASALT_QUIT_AFTER_MS.
gboolean quitAfterTimeout(gpointer data) {
  auto *app = static_cast<GApplication *>(data);
  g_message("BASALT_QUIT_AFTER_MS elapsed; quitting");
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

// BASALT_TEST_TAP: "x,y" pairs separated by ';', each fired a second apart.
// See GtkTouchDispatcher::synthesiseTap for why this exists.
struct PendingTap {
  Host *host;
  double x;
  double y;
};

gboolean fireTestTap(gpointer data) {
  auto *tap = static_cast<PendingTap *>(data);
  g_message("BASALT_TEST_TAP: tapping (%.0f, %.0f)", tap->x, tap->y);
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

// BASALT_TEST_HOVER: "x,y" points separated by ';', each fired a second apart.
// The pointer moving with no button down, which is the only way to reach the
// hover path from automation: a real cursor move needs a seat with input
// devices, and the whole point of these instruments is a run that has none. A
// negative point means the pointer left the surface.
//
// Reuses PendingTap: the payload is the same, and a second struct with the same
// three fields would be two things to keep in step.
gboolean fireTestHover(gpointer data) {
  std::unique_ptr<PendingTap> hover{static_cast<PendingTap *>(data)};
  g_message("BASALT_TEST_HOVER: hovering (%.0f, %.0f)", hover->x, hover->y);
  if (hover->host->touchDispatcher != nullptr) {
    hover->host->touchDispatcher->synthesiseHover(hover->x, hover->y);
  }
  return G_SOURCE_REMOVE;
}

// Returns the delay after the last point.
guint scheduleTestHovers(Host *host, const char *spec, guint delayMs) {
  char **points = g_strsplit(spec, ";", -1);
  for (char **point = points; *point != nullptr; ++point) {
    char **parts = g_strsplit(*point, ",", 2);
    if (parts[0] != nullptr && parts[1] != nullptr) {
      g_timeout_add(delayMs,
                    fireTestHover,
                    new PendingTap{host, g_ascii_strtod(parts[0], nullptr), g_ascii_strtod(parts[1], nullptr)});
      delayMs += 1000;
    }
    g_strfreev(parts);
  }
  g_strfreev(points);
  return delayMs;
}

// BASALT_TEST_FOCUS: keyboard actions separated by ';' -- `tab`, `shift-tab`,
// `activate`, `escape` and `devmenu`, each fired a second apart.
//
// The same reason the other instruments exist, one step further out. A real Tab
// needs a window the display server considers focused, and an automated run on
// a headless compositor has no such thing: the key event is delivered to
// nothing. This enters at GtkFocusManager, so it exercises GTK's own focus
// chain, the focus and blur events and the click dispatch, and skips only the
// delivery of the keystroke itself.
struct PendingFocus {
  Host *host;
  std::string action;
};

gboolean fireTestFocus(gpointer data) {
  std::unique_ptr<PendingFocus> pending{static_cast<PendingFocus *>(data)};
  g_message("BASALT_TEST_FOCUS: %s", pending->action.c_str());
  if (pending->host->focusManager == nullptr) {
    return G_SOURCE_REMOVE;
  }
  if (pending->action == "tab") {
    pending->host->focusManager->moveFocus(true);
  } else if (pending->action == "shift-tab") {
    pending->host->focusManager->moveFocus(false);
  } else if (pending->action == "activate") {
    pending->host->focusManager->activateFocused();
  } else if (pending->action == "devmenu") {
    // Also not a focus action. Same instrument for the same reason: a key that
    // needs nothing focused to arrive. What it opens is answered by
    // BASALT_TEST_MENU; see core/TestDialog.h.
    basalt::showDevMenu(pending->host->reactHost.get());
  } else if (pending->action == "escape") {
    // Not a focus action, and here anyway: this is the instrument for "a key
    // was pressed and nothing on the window has to be focused for it to
    // arrive", which is exactly what Escape closing a <Modal> is.
    pending->host->mountingManager->requestCloseTopModal();
  } else {
    g_warning("BASALT_TEST_FOCUS: unknown action \"%s\"", pending->action.c_str());
  }
  return G_SOURCE_REMOVE;
}

guint scheduleTestFocus(Host *host, const char *spec, guint delayMs) {
  char **actions = g_strsplit(spec, ";", -1);
  for (char **action = actions; *action != nullptr; ++action) {
    if (**action == '\0') {
      continue;
    }
    g_timeout_add(delayMs, fireTestFocus, new PendingFocus{host, *action});
    delayMs += 1000;
  }
  g_strfreev(actions);
  return delayMs;
}

// BASALT_TEST_SCROLL: "x,y,lines" triples separated by ';' -- a wheel over a
// point, a second apart, in surface-root coordinates. Positive lines scroll
// down, which is the direction `contentOffset` reads.
//
// The AppKit and Win32 hosts have had this since their scroll views did; this
// is the third, and it arrived with <RefreshControl>, whose gesture is a wheel
// that keeps asking to go up after the list has already reached its top.
struct PendingScroll {
  Host *host;
  double x;
  double y;
  double lines;
};

gboolean fireTestScroll(gpointer data) {
  std::unique_ptr<PendingScroll> pending{static_cast<PendingScroll *>(data)};
  Host *host = pending->host;
  if (host->mountingManager == nullptr || host->root == nullptr) {
    return G_SOURCE_REMOVE;
  }
  g_message("BASALT_TEST_SCROLL: %g lines at (%g, %g)", pending->lines, pending->x, pending->y);
  // Notches into pixels with the same constant a real wheel uses, so the
  // instrument and the hardware move a list by the same distance.
  host->mountingManager->scrollViews().scrollAt(
      host->root, pending->x, pending->y, 0.0, pending->lines * basalt::GtkScrollViewManager::kWheelStepPixels);
  return G_SOURCE_REMOVE;
}

guint scheduleTestScrolls(Host *host, const char *spec, guint delayMs) {
  char **steps = g_strsplit(spec, ";", -1);
  for (char **step = steps; *step != nullptr; ++step) {
    char **parts = g_strsplit(*step, ",", -1);
    if (g_strv_length(parts) == 3) {
      g_timeout_add(delayMs,
                    fireTestScroll,
                    new PendingScroll{host,
                                      g_ascii_strtod(parts[0], nullptr),
                                      g_ascii_strtod(parts[1], nullptr),
                                      g_ascii_strtod(parts[2], nullptr)});
      delayMs += 1000;
    }
    g_strfreev(parts);
  }
  g_strfreev(steps);
  return delayMs;
}

// BASALT_TEST_DRAG: "x1,y1,x2,y2" -- one press, twenty moves and a release, for
// the gestures a tap cannot reach. See GtkTouchDispatcher::synthesiseDrag.
struct PendingDrag {
  Host *host;
  double fromX;
  double fromY;
  double toX;
  double toY;
};

gboolean fireTestDrag(gpointer data) {
  std::unique_ptr<PendingDrag> drag{static_cast<PendingDrag *>(data)};
  g_message("BASALT_TEST_DRAG: (%.0f, %.0f) -> (%.0f, %.0f)",
            drag->fromX,
            drag->fromY,
            drag->toX,
            drag->toY);
  if (drag->host->touchDispatcher != nullptr) {
    drag->host->touchDispatcher->synthesiseDrag(
        drag->fromX, drag->fromY, drag->toX, drag->toY, 20);
  }
  return G_SOURCE_REMOVE;
}

void scheduleTestDrag(Host *host, const char *spec, guint delayMs) {
  char **parts = g_strsplit(spec, ",", 4);
  if (g_strv_length(parts) == 4) {
    g_timeout_add(delayMs,
                  fireTestDrag,
                  new PendingDrag{host,
                                  g_ascii_strtod(parts[0], nullptr),
                                  g_ascii_strtod(parts[1], nullptr),
                                  g_ascii_strtod(parts[2], nullptr),
                                  g_ascii_strtod(parts[3], nullptr)});
  }
  g_strfreev(parts);
}

// BASALT_TEST_TYPE: text to insert into whatever field has focus, for the
// same reason BASALT_TEST_TAP exists -- macOS cannot synthesise a real key
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
  g_message("BASALT_TEST_TYPE: typing \"%s\"", pending->text.c_str());

  GtkWidget *focus = pending->host->window != nullptr
      ? gtk_window_get_focus(pending->host->window)
      : nullptr;
  if (focus == nullptr || !GTK_IS_TEXT(focus)) {
    g_warning("BASALT_TEST_TYPE: no text field has focus");
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

// Ctrl+D: React Native's developer menu.
//
// The shortcut is React Native's own on a simulator, which is the closest
// thing a desktop has to a phone's shake. It does nothing in a release run --
// there is no dev server to reload from and no debugger to open -- so the key
// is left to travel on rather than swallowed.
gboolean onDevMenuKey(GtkEventControllerKey * /*controller*/,
                      guint keyval,
                      guint /*keycode*/,
                      GdkModifierType state,
                      gpointer data) {
  auto *host = static_cast<Host *>(data);
  if (!host->devMode || host->reactHost == nullptr) {
    return GDK_EVENT_PROPAGATE;
  }
  if ((keyval != GDK_KEY_d && keyval != GDK_KEY_D) ||
      (state & GDK_CONTROL_MASK) == 0) {
    return GDK_EVENT_PROPAGATE;
  }
  basalt::showDevMenu(host->reactHost.get());
  return GDK_EVENT_STOP;
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

  // A <Modal> is sized from its own shadow-node state rather than from a style,
  // and React Native's C++ platform answers "what size is the screen" with
  // zero. Told here, where the window's size is already being handed to Fabric.
  host->mountingManager->setSurfaceSize(static_cast<float>(width), static_cast<float>(height));
  // The window moved or was resized, which `useWindow()` hands an app as live
  // bounds. Here rather than in a watcher of its own: this is already the one
  // place that learns it.
  basalt::notifyWindowBoundsChanged();

  // The inspector covers the window, so it resizes with it.
  if (host->logBoxRoot != nullptr) {
    rn_view_set_frame(host->logBoxRoot, 0, 0, static_cast<float>(width), static_cast<float>(height));
    host->reactHost->setSurfaceConstraints(
        kLogBoxSurfaceId, constraintsFor(width, height), layoutContextFor(host->scaleFactor));
  }
}

// Starts or stops React Native's error inspector.
//
// It is `LogBoxInspectorContainer`, registered by AppRegistry under the name
// "LogBox" exactly as an app registers its own component -- so this is a second
// surface rather than an overlay this host draws, and everything in it is React
// Native's own JavaScript. See core/LogBoxSurface.h.
//
// Started and stopped rather than kept and hidden: a surface that exists is a
// React tree that renders and re-renders, and the inspector is showing for a
// vanishingly small part of a session.
void hideLogBoxSurface(Host *host) {
  if (host->reactHost == nullptr || host->logBoxRoot == nullptr) {
    return;
  }
  host->reactHost->stopSurface(kLogBoxSurfaceId);
  gtk_overlay_remove_overlay(GTK_OVERLAY(host->overlay), GTK_WIDGET(host->logBoxRoot));
  host->mountingManager->destroySurfaceRoot(kLogBoxSurfaceId);
  host->logBoxRoot = nullptr;
}

void showLogBoxSurface(Host *host, const std::string &appKey) {
  if (host->reactHost == nullptr || host->overlay == nullptr || host->logBoxRoot != nullptr) {
    return;
  }

  const int width = gtk_widget_get_width(GTK_WIDGET(host->root));
  const int height = gtk_widget_get_height(GTK_WIDGET(host->root));
  if (width <= 0 || height <= 0) {
    return;
  }

  host->logBoxRoot = host->mountingManager->createSurfaceRoot(kLogBoxSurfaceId);
  rn_view_set_frame(host->logBoxRoot, 0, 0, static_cast<float>(width), static_cast<float>(height));
  // Added after the window controls, so it is above them: an error box that a
  // close button sits on top of is one the user can close by accident.
  gtk_widget_set_halign(GTK_WIDGET(host->logBoxRoot), GTK_ALIGN_FILL);
  gtk_widget_set_valign(GTK_WIDGET(host->logBoxRoot), GTK_ALIGN_FILL);
  gtk_overlay_add_overlay(GTK_OVERLAY(host->overlay), GTK_WIDGET(host->logBoxRoot));

  host->reactHost->startSurface(kLogBoxSurfaceId,
                                appKey,
                                folly::dynamic::object(),
                                constraintsFor(width, height),
                                layoutContextFor(host->scaleFactor));
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
facebook::react::TurboModuleProviders makeTurboModuleProviders(std::string scriptURL,
                                                              bool devMode) {
  facebook::react::TurboModuleProviders providers;
  providers.emplace_back(
      [scriptURL = std::move(scriptURL), devMode](const std::string &name,
         const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == facebook::react::PlatformConstantsModule::kModuleName) {
          return std::make_shared<basalt::DesktopPlatformConstantsModule>(jsInvoker);
        }
        if (name == basalt::DesktopAppearanceModule::kModuleName) {
          return std::make_shared<basalt::DesktopAppearanceModule>(jsInvoker);
        }
        if (name == basalt::DesktopBlobModule::kModuleName) {
          return std::make_shared<basalt::DesktopBlobModule>(jsInvoker);
        }
        if (name == basalt::DesktopFileReaderModule::kModuleName) {
          return std::make_shared<basalt::DesktopFileReaderModule>(jsInvoker);
        }
        if (name == basalt::DesktopClipboardModule::kModuleName) {
          return std::make_shared<basalt::DesktopClipboardModule>(jsInvoker);
        }
        if (name == basalt::DesktopVibrationModule::kModuleName) {
          return std::make_shared<basalt::DesktopVibrationModule>(jsInvoker);
        }
        if (name == basalt::DesktopAlertModule::kModuleName) {
          return std::make_shared<basalt::DesktopAlertModule>(jsInvoker);
        }
        if (name == basalt::DesktopLinkingModule::kModuleName) {
          return std::make_shared<basalt::DesktopLinkingModule>(jsInvoker);
        }
        if (name == basalt::DesktopShareModule::kModuleName) {
          return std::make_shared<basalt::DesktopShareModule>(jsInvoker);
        }
        // The native file dialogs, which React Native has no API for and which
        // are the first thing a desktop app reaches for. See core/DialogModule.h.
        if (name == basalt::DesktopDialogModule::kModuleName) {
          return std::make_shared<basalt::DesktopDialogModule>(jsInvoker);
        }
        if (name == basalt::DesktopI18nManagerModule::kModuleName) {
          return std::make_shared<basalt::DesktopI18nManagerModule>(jsInvoker);
        }
        if (name == basalt::DesktopAccessibilityManagerModule::kModuleName) {
          return std::make_shared<basalt::DesktopAccessibilityManagerModule>(jsInvoker);
        }
        if (name == basalt::DesktopAccessibilityInfoModule::kModuleName) {
          return std::make_shared<basalt::DesktopAccessibilityInfoModule>(jsInvoker);
        }
        if (name == basalt::DesktopToastModule::kModuleName) {
          return std::make_shared<basalt::DesktopToastModule>(jsInvoker);
        }
        // react-native-gesture-handler's module. Offered whether or not the app
        // has the library: it costs a name comparison, and the alternative is a
        // host that has to be rebuilt to run an app that uses gestures.
        if (name == basalt::DesktopGestureHandlerModule::kModuleName) {
          return std::make_shared<basalt::DesktopGestureHandlerModule>(jsInvoker);
        }
#ifdef BASALT_HAS_WORKLETS
        // react-native-worklets, when the build was pointed at one. Reanimated
        // is built on it; see core/WorkletsModule.h.
        if (name == basalt::DesktopWorkletsModule::kModuleName) {
          return std::make_shared<basalt::DesktopWorkletsModule>(jsInvoker);
        }
#endif
#ifdef BASALT_HAS_REANIMATED
        if (name == basalt::DesktopReanimatedModule::kModuleName) {
          return std::make_shared<basalt::DesktopReanimatedModule>(jsInvoker);
        }
#endif
        // Only outside dev mode. ReactCxxPlatform provides a real DevSettings
        // when a dev server exists, and this provider is consulted first -- so
        // offering ours unconditionally would shadow the one that works.
        if (!devMode && name == basalt::DesktopDevSettingsModule::kModuleName) {
          return std::make_shared<basalt::DesktopDevSettingsModule>(jsInvoker);
        }
        if (name == basalt::DesktopStatusBarModule::kModuleName) {
          return std::make_shared<basalt::DesktopStatusBarModule>(jsInvoker);
        }
        // The title bar. Reaches the window through the one GtkTitleBar, which
        // the host attached at startup -- a module cannot be handed a window it
        // is created before.
        if (name == basalt::GtkWindowModule::kModuleName) {
          return std::make_shared<basalt::GtkWindowModule>(jsInvoker);
        }
        // Only outside dev mode, for the same reason DevSettings is above.
        //
        // This module's scriptURL is what HMRClient registers with Metro as its
        // entry point, and Metro resolves that URL to a *graph*. The URL
        // synthesised here is not the one DevServerHelper actually fetched --
        // that one carries lazy, minify, runModule and app -- so it names a
        // graph nobody built, Metro answers GraphNotFoundError, and Fast
        // Refresh silently never starts. ReactCxxPlatform's own SourceCode
        // module reports the URL the bundle really came from, which is the one
        // whose graph exists. See plan/48-fast-refresh.md.
        if (!devMode && name == basalt::DesktopSourceCodeModule::kModuleName) {
          return std::make_shared<basalt::DesktopSourceCodeModule>(jsInvoker, scriptURL);
        }
        return nullptr;
      });
  return providers;
}

// Runs against the JavaScript runtime before the bundle is evaluated, which is
// the only moment early enough for what goes in here.
void installBindings(facebook::jsi::Runtime &runtime) {
  basalt::installExpoRuntime(runtime);
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
  gtk_window_set_title(host->window, "react-native-basalt");
  gtk_window_set_default_size(host->window, kInitialWidth, kInitialHeight);
  host->scaleFactor = gtk_widget_get_scale_factor(GTK_WIDGET(host->window));

  // Constructed here, on the GTK main thread: GtkMountingManager records this
  // thread and asserts that every widget mutation lands back on it.
  host->mountingManager = std::make_shared<basalt::GtkMountingManager>();

  // Fabric emits no Create for a surface root -- the root shadow node is the
  // base of every diff, so it has to exist before the surface starts.
  host->root = host->mountingManager->createSurfaceRoot(kSurfaceId);

  // The surface root, with room above it for the window controls the host
  // draws when the title bar is hidden. GTK takes the whole titlebar away with
  // the decorations -- buttons included -- where Windows and macOS keep
  // drawing theirs over the app's content, so on this desktop the host has to
  // put them back. An overlay is how: the root fills the window and the
  // controls sit over its top corner.
  GtkWidget *overlay = gtk_overlay_new();
  host->overlay = overlay;
  gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(host->root));

  GtkWidget *controls = gtk_window_controls_new(GTK_PACK_END);
  gtk_widget_set_halign(controls, GTK_ALIGN_END);
  gtk_widget_set_valign(controls, GTK_ALIGN_START);
  // Hidden until an app asks for the hidden style; see GtkTitleBar.h.
  gtk_widget_set_visible(controls, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), controls);

  gtk_window_set_child(host->window, overlay);
  basalt::titleBar().attach(host->window, controls);
  rn_view_set_resize_callback(host->root, onRootResized, host);

  // Maximised and full screen, which change the bounds and not the size of the
  // surface -- so the resize callback above never runs for them. GTK has no
  // move notification and no position to report; see GtkWindowControl.cpp.
  const auto onWindowState = +[](GObject * /*window*/, GParamSpec * /*spec*/, gpointer /*data*/) {
    basalt::notifyWindowBoundsChanged();
  };
  g_signal_connect(host->window, "notify::maximized", G_CALLBACK(onWindowState), nullptr);
  g_signal_connect(host->window, "notify::fullscreened", G_CALLBACK(onWindowState), nullptr);

  // Prime the bounds cache, which `getBounds()` answers from: without this an
  // app's first render sees a window of no size, and only a later resize
  // corrects it. See core/WindowBoundsCache.cpp.
  basalt::notifyWindowBoundsChanged();
  g_signal_connect(host->root, "map", G_CALLBACK(onRootMapped), host);

  host->runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
  host->choreographer = std::make_shared<basalt::GtkAnimationChoreographer>();

  // Light or dark, and any change to it. Before ReactHost, so the module can
  // answer the first `Appearance.getColorScheme()` a bundle makes, which for an
  // Expo app is during its first import.
  basalt::startObservingColorScheme();

  // Before ReactHost, so the beat is being induced from the first event on.
  host->runLoopObserver = basalt::installRunLoopObserver(host->runLoopObserverManager);

  // Input. Attached to the root, which is where hit testing starts.
  host->touchDispatcher =
      std::make_unique<basalt::GtkTouchDispatcher>(host->mountingManager.get(), host->root);
  // The keyboard half: Tab reaching a <Pressable>, and Enter activating it.
  host->focusManager =
      std::make_unique<basalt::GtkFocusManager>(host->mountingManager.get(), host->root);

  // Ctrl+D, which is React Native's developer menu. Its own controller rather
  // than another case in the focus manager's: this has nothing to do with
  // focus, and a shortcut that works wherever the caret is has to be on the
  // window in the capture phase anyway -- otherwise a <TextInput> with the
  // keyboard would eat it.
  GtkEventController *devKeys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(devKeys, GTK_PHASE_CAPTURE);
  g_signal_connect(devKeys, "key-pressed", G_CALLBACK(onDevMenuKey), host);
  gtk_widget_add_controller(GTK_WIDGET(host->window), devKeys);

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
  // The appId is how this host tells Metro which platform it is. ReactCxxPlatform's
  // DevServerHelper builds its bundle URL from `constexpr DEFAULT_PLATFORM =
  // "android"` with no hook and no setting, and `app=` -- which comes from
  // here, and which Metro itself ignores -- is the only thing in that URL a
  // host controls. The Metro plugin reads it back and corrects the platform.
  // See APP_ID_PREFIX in packages/react-native-basalt/metro-config.js.
  config.appId = "basalt-linux";
  config.deviceName = "linux";

  // Dev mode changes three things at once, which is worth being explicit about:
  // loadScript tries Metro before the on-disk bundle; DevServerHelper exists,
  // which is the only condition under which ReactCxxTurboModuleProvider serves
  // the DevSettings module a __DEV__ bundle requires; and ReactHost opens a
  // packager connection whose reload message reloads the instance.
  config.enableDevMode = g_getenv("BASALT_DEV") != nullptr;
  host->devMode = config.enableDevMode;
  config.enableInspector = config.enableDevMode;
  if (const char *devHost = g_getenv("BASALT_DEV_HOST")) {
    config.devServerHost = devHost;
  }
  if (const char *devPort = g_getenv("BASALT_DEV_PORT")) {
    config.devServerPort = static_cast<uint32_t>(g_ascii_strtoull(devPort, nullptr, 10));
  }
  if (config.enableDevMode) {
    g_message("dev mode: Metro at %s:%u, entry '%s'",
              config.devServerHost.c_str(),
              config.devServerPort,
              host->sourcePath.c_str());
    // So the http client can tell the bundle request apart from an app's own
    // fetch, and refuse to feed a Metro error page to the JS engine. See
    // core/DevBundle.h.
    basalt::setDevServerOrigin(config.devServerHost, config.devServerPort);
  }

  try {
    host->reactHost = std::make_unique<ReactHost>(config,
                                                  host->mountingManager,
                                                  host->runLoopObserverManager,
                                                  makeContextContainer(),
                                                  facebook::react::getDefaultOnJsErrorFunc(),
                                                  logToGlib,
                                                  nullptr,
                                                  makeTurboModuleProviders(basalt::scriptURLFor(
                                                      host->bundlePath,
                                                      config.enableDevMode,
                                                      config.devServerHost,
                                                      config.devServerPort,
                                                      host->sourcePath.empty() ? "index"
                                                                               : host->sourcePath,
                                                      "linux"),
                                                                  config.enableDevMode),
                                                  // React Native's error
                                                  // inspector. ReactCxxPlatform
                                                  // implements the LogBox
                                                  // TurboModule itself and only
                                                  // provides it when a host
                                                  // hands over one of these;
                                                  // passing null, as this did,
                                                  // left NativeLogBox.show() a
                                                  // call into nothing. See
                                                  // core/LogBoxSurface.h.
                                                  std::make_shared<basalt::LogBoxSurfaceDelegate>(
                                                      [host](const std::string &appKey) {
                                                        showLogBoxSurface(host, appKey);
                                                      },
                                                      [host]() { hideLogBoxSurface(host); }),
                                                  // `useNativeDriver: true`.
                                                  // ReactCommon has a C++
                                                  // implementation of the whole
                                                  // animated graph and
                                                  // ReactCxxPlatform provides
                                                  // the module for it, but only
                                                  // when a host asks by handing
                                                  // over a provider. Without it
                                                  // every native-driven
                                                  // Animated call throws
                                                  // "Native animated module is
                                                  // not available".
                                                  std::make_shared<
                                                      facebook::react::
                                                          NativeAnimatedNodesManagerProvider>(),
                                                  installBindings,
                                                  host->choreographer);
  } catch (const std::exception &error) {
    g_error("could not construct ReactHost: %s", error.what());
  }

  // How anything in the core reaches the scheduler, which only the host has.
  // Reanimated is the one caller; see core/UIManagerAccess.h.
  basalt::setEventListenerInstaller(
      [host](std::shared_ptr<const facebook::react::EventListener> listener) {
        if (host->reactHost == nullptr) {
          return;
        }
        host->reactHost->runOnScheduler(
            [listener = std::move(listener)](facebook::react::Scheduler &scheduler) {
              scheduler.addEventListener(listener);
            });
      });

  // `loadScript` falls back to the on-disk bundle whenever the Metro fetch
  // fails, which is right when nothing is listening and wrong when Metro
  // answered with an error: running the last bundle that built, while the
  // developer looks at source that is not what is executing, hides the very
  // thing they need to see. So the fallback is allowed only for the first case,
  // and the second stops here with Metro's own message.
  const bool loaded = host->reactHost->loadScript(host->bundlePath, host->sourcePath);
  if (const auto devError = basalt::devBundleError()) {
    g_warning("Metro could not build the bundle (HTTP %ld):\n%s",
              devError->status,
              devError->message.c_str());
    // Non-zero, so a script that starts this host can tell a build failure from
    // a run that ended. The AppKit host returns 1 from main for the same reason.
    host->exitStatus = 1;
    g_application_quit(G_APPLICATION(gtk_window_get_application(host->window)));
    return;
  }
  if (!loaded) {
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
  if (const char *taps = g_getenv("BASALT_TEST_TAP")) {
    scriptedDelayMs = scheduleTestTaps(host, taps);
  }
  if (const char *hovers = g_getenv("BASALT_TEST_HOVER")) {
    scriptedDelayMs = scheduleTestHovers(host, hovers, scriptedDelayMs);
  }
  if (const char *scrolls = g_getenv("BASALT_TEST_SCROLL")) {
    scriptedDelayMs = scheduleTestScrolls(host, scrolls, scriptedDelayMs);
  }
  if (const char *focus = g_getenv("BASALT_TEST_FOCUS")) {
    scriptedDelayMs = scheduleTestFocus(host, focus, scriptedDelayMs);
  }
  if (const char *drag = g_getenv("BASALT_TEST_DRAG")) {
    scheduleTestDrag(host, drag, scriptedDelayMs);
    scriptedDelayMs += 1000;
  }
  if (const char *text = g_getenv("BASALT_TEST_TYPE")) {
    g_timeout_add(scriptedDelayMs, fireTestType, new PendingType{host, text});
  }
  g_unix_signal_add(SIGINT, quitOnSignal, app);
  g_unix_signal_add(SIGTERM, quitOnSignal, app);

  if (const char *quitAfter = g_getenv("BASALT_QUIT_AFTER_MS")) {
    const gint64 ms = g_ascii_strtoll(quitAfter, nullptr, 10);
    if (ms > 0) {
      g_timeout_add(static_cast<guint>(ms), quitAfterTimeout, app);
    }
  }
}

// BASALT_DUMP_TREE: write the widget tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot. See docs/TESTING.md.
void dumpTreeIfRequested(Host *host) {
  const char *path = g_getenv("BASALT_DUMP_TREE");
  if (path == nullptr || host->root == nullptr) {
    return;
  }
  char *appTree = rn_view_describe_tree(host->root);
  // The error inspector is a second surface with a root of its own, so it is
  // invisible to a dump of the app's. Appended rather than merged, because the
  // two are siblings on screen and nesting one inside the other would say
  // something untrue about the tree. Same separator the AppKit host writes.
  char *description = appTree;
  char *logBoxTree = nullptr;
  if (host->logBoxRoot != nullptr) {
    logBoxTree = rn_view_describe_tree(host->logBoxRoot);
    description = g_strconcat(appTree, "--- LogBox ---\n", logBoxTree, nullptr);
    g_free(appTree);
    g_free(logBoxTree);
  }
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
  basalt::removeRunLoopObserver(host->runLoopObserver);
  host->runLoopObserver = nullptr;
  host->focusManager.reset();
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
  host.moduleName = argc > 2 ? argv[2] : "BasaltDemo";

  // A URL the desktop launched this with, for `Linking.getInitialURL()`. Any
  // argument after the bundle and the module that carries a scheme: a
  // `.desktop` entry's `%%u`, a registered scheme, a shell association. Matched
  // rather than positional, because a launcher appends it and does not know
  // what came before.
  for (int i = 1; i < argc; i++) {
    const std::string argument(argv[i]);
    if (argument.find("://") != std::string::npos) {
      basalt::setInitialUrl(argument);
      break;
    }
  }
  const char *sourcePath = g_getenv("BASALT_DEV_ENTRY");
  host.sourcePath = sourcePath != nullptr ? sourcePath : "index";

  // The app's Expo config, if this is an Expo app and it has been bundled.
  // Before anything installs the runtime. See core/ExpoModules.h.
  basalt::loadExpoAppConfigBeside(host.bundlePath);

  // What this app calls itself, which a generic host binary cannot know. Read
  // before the GtkApplication, because the application id is construct-only and
  // is what a desktop matches a running window to its `.desktop` file with --
  // and therefore what puts the app's own name and icon on its notifications.
  // See core/AppIdentity.h and cli/packageApp.js.
  const basalt::AppIdentity &identity = basalt::appIdentity(host.bundlePath);

  // GTK would try to interpret argv as files to open. The bundle path is ours,
  // so the application never sees it.
  //
  // `dev.basalt.host` is the fallback rather than the rule: an app that was
  // started through `react-native run-linux` has an identity file and gets its
  // own id. GTK requires a valid D-Bus name, and an identifier that is not one
  // would make gtk_application_new fail outright, so it is checked first.
  const char *applicationId =
      (!identity.identifier.empty() && g_application_id_is_valid(identity.identifier.c_str()))
      ? identity.identifier.c_str()
      : "dev.basalt.host";
  GtkApplication *app = gtk_application_new(applicationId, G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(onActivate), &host);
  g_signal_connect(app, "shutdown", G_CALLBACK(onShutdown), &host);

  const int status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return host.exitStatus != 0 ? host.exitStatus : status;
}
