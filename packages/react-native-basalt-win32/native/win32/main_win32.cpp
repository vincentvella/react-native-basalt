// react-native-basalt — the Windows host process.
//
// Constructs a ReactHost, loads a script into Hermes, and runs one surface in
// an HWND. Everything on screen from here on is produced by Fabric: the
// mutation stream arrives through Win32MountingManager exactly as it does on
// iOS and Android, and nothing hand-builds a ShadowViewMutation.
//
// The counterpart of gtk/main_gtk.cpp and appkit/main_appkit.mm, and
// deliberately the same shape. The pieces a host has to supply, and where each
// comes from:
//
//   IMountingManager         Win32MountingManager         (this repo)
//   RunLoopObserverManager   ReactCxxPlatform             (event beat)
//   the beat itself          Win32RunLoopObserver         (this repo, the loop)
//   AnimationChoreographer   Win32AnimationChoreographer  (this repo, a timer)
//   ContextContainer         http + websocket client factories, below
//   ComponentRegistryFactory ComponentRegistryWin32       (via the mounting manager)
//   FontRegistry             FontRegistryDirectWrite      (this repo, link-time seam)
//   input                    Win32TouchDispatcher         (this repo, hostProc)
//
// Threading: this thread owns the window and the message loop. ReactHost spins
// up its own JS thread, and every mount is marshalled back here by
// Win32MountingManager through postToUiThread. Nothing below touches a view off
// this thread.
//
// What this host can render is what Win32MountingManager can mount: <View>,
// <Text>, <Image>, <ScrollView> and <TextInput> -- the same five as the other
// two desktops. `js/demo.js` -- a surface driven straight through
// nativeFabricUIManager, with no React and no react-native JavaScript -- is
// still the bundle this runs by default, because it is what an argumentless run
// has always meant here; an ordinary React app runs from a bundle and a module
// name.
//
// One thing below is not like the other hosts, and it is <TextInput>. Its peer
// is a real EDIT control, which is a child *window*, so the host has to place
// it, forward its notifications, and answer its colour questions. See
// win32/Win32TextInput.h.

#include "Win32AnimationChoreographer.h"
#include "Win32MountingManager.h"
#include "Win32RunLoopObserver.h"
#include "Win32Snapshot.h"
#include "Win32Strings.h"
#include "LogBoxSurface.h"

#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include "Win32Focus.h"
#include "Win32TouchDispatcher.h"
#include "Win32UiThread.h"
#include "Win32TitleBar.h"
#include "Win32WindowModule.h"
#include "RnWin32View.h"

#include "AppearanceModule.h"
#include "BlobModule.h"
#include "ColorScheme.h"
#include "CoreModules.h"
#include "DevBundle.h"
#include "ExpoModules.h"
#include "ExpoRuntime.h"
#include "GestureHandlerModule.h"
#include "PlatformConstantsModule.h"
#include "PlatformServices.h"
#include "ReanimatedModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"
#include "UIManagerAccess.h"
#include "WorkletsModule.h"

#include <jsi/jsi.h>
#include <logger/react_native_log.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>
#include <react/logging/DefaultOnJsErrorHandler.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/runtime/ReactHost.h>
#include <react/runtime/ReactInstanceConfig.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <windows.h>
// GET_X_LPARAM. Its own header, and not pulled in by WIN32_LEAN_AND_MEAN.
#include <windowsx.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using facebook::react::ContextContainer;
using facebook::react::Float;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::LayoutDirection;
using facebook::react::ReactHost;
using facebook::react::ReactInstanceConfig;
using facebook::react::RunLoopObserverManager;
using facebook::react::SurfaceId;
using basalt::win32::RnWin32View;

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

constexpr UINT_PTR kSecondTreeTimer = 100;

struct Host {
  HWND window{nullptr};
  RnWin32View *root{nullptr};

  ComPtr<ID2D1Factory> d2dFactory;
  ComPtr<ID2D1HwndRenderTarget> target;

  std::shared_ptr<basalt::Win32MountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<basalt::Win32AnimationChoreographer> choreographer;
  std::unique_ptr<ReactHost> reactHost;
  std::unique_ptr<basalt::Win32TouchDispatcher> touchDispatcher;
  std::unique_ptr<basalt::Win32FocusManager> focusManager;
  // The error inspector's own surface root, or null when it is not showing.
  // Painted after the app's and hit-tested before it, which is the whole of
  // what "on top" means on a platform where a view is not a window.
  RnWin32View *logBoxRoot{nullptr};

  std::string bundlePath;
  // Empty means the bundle is a raw Fabric script rather than a React app.
  std::string moduleName;
  // Metro's entry point, without the extension. Only meaningful in dev mode.
  std::string sourcePath;
  bool surfaceStarted{false};
  int scaleFactor{1};
  // Whether TrackMouseEvent is armed. Windows sends WM_MOUSELEAVE once and then
  // forgets, so it has to be re-armed on every move; the flag keeps that to one
  // call per entry rather than one per motion message.
  bool trackingMouseLeave{false};
};

Host gHost;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// ReactHost's Logger is where console.log and RN's own logging come out.
// stderr rather than OutputDebugString: this is a console subsystem binary, and
// a host whose logs are only visible in a debugger is a host nobody can run
// from a terminal.
void logToConsole(const std::string &message, unsigned int logLevel) {
  const char *prefix = "[js]";
  switch (logLevel) {
    case ReactNativeLogLevelFatal:
    case ReactNativeLogLevelError:
      prefix = "[js error]";
      break;
    case ReactNativeLogLevelWarning:
      prefix = "[js warn]";
      break;
    default:
      break;
  }
  std::fprintf(stderr, "%s %s\n", prefix, message.c_str());
  std::fflush(stderr);
}

// ---------------------------------------------------------------------------
// Layout constraints
// ---------------------------------------------------------------------------

// Minimum == maximum pins the root to the window's client box, which is what a
// full-window surface wants: the root fills, and flex children divide it.
LayoutConstraints constraintsFor(int width, int height) {
  const facebook::react::Size size{static_cast<Float>(width), static_cast<Float>(height)};
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
// Painting
// ---------------------------------------------------------------------------

bool ensureTarget() {
  if (gHost.target) {
    return true;
  }
  if (!gHost.d2dFactory || gHost.window == nullptr) {
    return false;
  }
  RECT client{};
  GetClientRect(gHost.window, &client);
  const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(client.right - client.left),
                                       static_cast<UINT32>(client.bottom - client.top));
  if (size.width == 0 || size.height == 0) {
    return false;
  }
  return SUCCEEDED(gHost.d2dFactory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(gHost.window, size),
      &gHost.target));
}

// Asked for after every mounted transaction. Windows repaints on demand rather
// than continuously, so without this a mutation changes the view tree and
// nothing on screen moves until the window happens to be invalidated by
// something else -- which reads as "mounting is broken" and is not.
void requestRepaint() {
  if (gHost.window != nullptr) {
    InvalidateRect(gHost.window, nullptr, FALSE);
  }
}

// After every transaction, and after every scroll. Both move views, and a
// <TextInput>'s peer is a child window that does not move with one: nothing in
// the view tree owns an HWND, so its placement is recomputed from the tree
// rather than following it. GTK and AppKit need no equivalent, because there a
// text field is a widget inside the widget that is the view.
void syncPeersAndRepaint() {
  if (gHost.mountingManager != nullptr) {
    gHost.mountingManager->syncTextInputBounds(gHost.root);
  }
  requestRepaint();
}

// ---------------------------------------------------------------------------
// Calling into JS
// ---------------------------------------------------------------------------

// Runs on the JS thread. `step` lets the script commit a second, different tree
// so an Update/Remove diff can be seen coming out of Fabric rather than only an
// initial mount.
void callRenderFunction(int step) {
  if (gHost.reactHost == nullptr) {
    return;
  }
  gHost.reactHost->runOnRuntimeScheduler([step](facebook::jsi::Runtime &runtime) {
    try {
      auto value = runtime.global().getProperty(runtime, kRenderFunctionName);
      if (!value.isObject() || !value.getObject(runtime).isFunction(runtime)) {
        std::fprintf(stderr, "script defines no global %s()\n", kRenderFunctionName);
        return;
      }
      value.getObject(runtime).getFunction(runtime).call(
          runtime,
          facebook::jsi::Value(static_cast<int>(kSurfaceId)),
          facebook::jsi::Value(step));
    } catch (const facebook::jsi::JSError &error) {
      std::fprintf(stderr,
                   "%s() threw: %s\n%s\n",
                   kRenderFunctionName,
                   error.getMessage().c_str(),
                   error.getStack().c_str());
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s() failed: %s\n", kRenderFunctionName, error.what());
    }
  });
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

// Feature flags this platform states explicitly. Anything not listed keeps
// React Native's own default.
class DesktopFeatureFlags : public facebook::react::ReactNativeFeatureFlagsDefaults {
 public:
  bool enableBridgelessArchitecture() override {
    return true;
  }
};

// TurboModules this platform supplies itself. ReactCxxTurboModuleProvider
// consults these before its own, so naming a module it also provides replaces
// it. Every one comes from core/ and is shared with the other two hosts --
// which is the whole argument of phase 17 arriving as a list of things this
// file did not have to write.
facebook::react::TurboModuleProviders makeTurboModuleProviders(std::string scriptURL,
                                                               bool devMode) {
  facebook::react::TurboModuleProviders providers;
  providers.emplace_back(
      [scriptURL = std::move(scriptURL), devMode](
          const std::string &name,
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
        // The window's title bar. This host's own rather than core's, because it
        // needs the window; see win32/Win32WindowModule.h.
        if (name == basalt::Win32WindowModule::kModuleName) {
          return std::make_shared<basalt::Win32WindowModule>(jsInvoker);
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

  // ReactHost throws without these two. Both are real implementations --
  // curl-backed http from core/HttpClient.cpp, and React Native's own
  // boost::beast websocket client -- and neither is about a toolkit, which is
  // why this host gets them for free.
  contextContainer->insert(facebook::react::HttpClientFactoryKey,
                           facebook::react::getHttpClientFactory());
  contextContainer->insert(facebook::react::WebSocketClientFactoryKey,
                           facebook::react::getWebSocketClientFactory());

  // MessageQueueThreadFactoryKey is deliberately left unset: ReactHost then
  // installs MessageQueueThreadImpl, a real threaded queue.
  return contextContainer;
}

// BASALT_DUMP_TREE: write the view tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot.
void dumpTreeIfRequested() {
  const char *path = std::getenv("BASALT_DUMP_TREE");
  if (path == nullptr || gHost.root == nullptr) {
    return;
  }
  if (FILE *file = std::fopen(path, "wb")) {
    std::string described = gHost.root->describeTree();
    // The error inspector is a second surface with a root of its own, so it is
    // invisible to a dump of the app's. Appended rather than merged, because
    // the two are siblings on screen and nesting one inside the other would say
    // something untrue about the tree. Same separator the other two hosts use.
    if (gHost.logBoxRoot != nullptr) {
      described += "--- LogBox ---\n";
      described += gHost.logBoxRoot->describeTree();
    }
    std::fwrite(described.data(), 1, described.size(), file);
    std::fclose(file);
    std::fprintf(stderr, "wrote view tree to %s\n", path);
  } else {
    std::fprintf(stderr, "could not write %s\n", path);
  }
}

// BASALT_SNAPSHOT: render the surface to a PNG on the way out.
//
// The tree dump says which mutations arrived; it says nothing about whether any
// of them reached the screen, and on Windows those are genuinely separate --
// painting only happens when something invalidates. This is offscreen rather
// than a screen grab, so it works on a machine with no one watching and needs
// no window to be visible.
void snapshotIfRequested() {
  const char *path = std::getenv("BASALT_SNAPSHOT");
  if (path == nullptr || gHost.root == nullptr) {
    return;
  }
  if (basalt::win32::writeSnapshot(*gHost.root, path)) {
    std::fprintf(stderr, "wrote snapshot to %s\n", path);
  } else {
    std::fprintf(stderr, "could not write %s\n", path);
  }
}

// ---------------------------------------------------------------------------
// Scripted input
// ---------------------------------------------------------------------------

// BASALT_TEST_TAP and BASALT_TEST_DRAG, the same two escape hatches both other
// hosts have and for the same reason: the input path is otherwise untestable in
// automation. Synthesising a real click on Windows means SendInput, which moves
// the actual cursor and so cannot run beside anything else on the machine --
// the same objection CGEvent raises on macOS.
//
// They enter at the dispatcher rather than at the window procedure, so what
// they prove is hit testing, emitter lookup and delivery through the event
// beat, and what they leave unproven is that Windows routes WM_LBUTTONDOWN to
// `hostProc` at all. A person clicking the window is still the only check on
// that half, on all three platforms.
struct ScriptedInput {
  enum class Kind { Tap, Hover, Drag, Wheel, Type, Focus };

  Kind kind{Kind::Tap};
  double fromX{0};
  double fromY{0};
  double toX{0};
  double toY{0};
  // Wheel only: notches, positive being a turn towards the user, which scrolls
  // content down. The sign is the one BASALT_TEST_SCROLL takes on the other
  // hosts, not the one WM_MOUSEWHEEL uses.
  double lines{0};
  // Type only. Default-initialised explicitly, so that the three kinds that do
  // not carry text can leave it out of a designated initialiser.
  std::string text{};
};

// Fired from timers keyed by index, so the vector has to outlive the loop.
std::vector<ScriptedInput> gScriptedInput;

constexpr UINT_PTR kScriptedInputTimerBase = 200;

// The numbers in "10,20,30" -- however many there are, which is what lets one
// parser read both a tap's pair and a drag's four.
std::vector<double> parseNumbers(const std::string &spec) {
  std::vector<double> numbers;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string piece =
        spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!piece.empty()) {
      numbers.push_back(std::strtod(piece.c_str(), nullptr));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return numbers;
}

void CALLBACK fireScriptedInput(HWND hwnd, UINT, UINT_PTR id, DWORD) {
  KillTimer(hwnd, id);
  const size_t index = static_cast<size_t>(id - kScriptedInputTimerBase);
  if (index >= gScriptedInput.size()) {
    return;
  }
  const ScriptedInput &action = gScriptedInput[index];
  switch (action.kind) {
    case ScriptedInput::Kind::Tap:
      std::fprintf(stderr, "BASALT_TEST_TAP: tapping (%.0f, %.0f)\n", action.fromX, action.fromY);
      if (gHost.touchDispatcher != nullptr) {
        gHost.touchDispatcher->synthesiseTap(action.fromX, action.fromY);
      }
      // A real click on a <TextInput> never reaches the touch dispatcher: the
      // peer is a child window, so USER32 routes the click to it and the
      // control focuses itself. That is the one thing a synthesised tap cannot
      // reproduce, so it is done here instead -- and only here, because the
      // real path needs none of it.
      if (gHost.mountingManager != nullptr &&
          gHost.mountingManager->focusTextInputAt(gHost.root, action.fromX, action.fromY)) {
        std::fprintf(stderr, "BASALT_TEST_TAP: focused the field there\n");
      }
      break;

    case ScriptedInput::Kind::Hover:
      std::fprintf(stderr,
                   "BASALT_TEST_HOVER: hovering (%.0f, %.0f)\n",
                   action.fromX,
                   action.fromY);
      if (gHost.touchDispatcher != nullptr) {
        gHost.touchDispatcher->synthesiseHover(action.fromX, action.fromY);
      }
      break;

    case ScriptedInput::Kind::Drag:
      std::fprintf(stderr,
                   "BASALT_TEST_DRAG: (%.0f, %.0f) -> (%.0f, %.0f)\n",
                   action.fromX,
                   action.fromY,
                   action.toX,
                   action.toY);
      gHost.touchDispatcher->synthesiseDrag(
          action.fromX, action.fromY, action.toX, action.toY, 20);
      break;

    case ScriptedInput::Kind::Wheel: {
      // A real WM_MOUSEWHEEL through the real window procedure, unlike the tap
      // and the drag above -- because the two things most likely to be wrong
      // about a wheel on Windows are the ones a dispatcher-level injection
      // would skip. Its coordinates are in screen space and its sign is
      // inverted against a contentOffset, and either mistake produces a wheel
      // that scrolls the wrong thing or the wrong way rather than one that does
      // nothing. So the point is converted back out to screen coordinates here
      // for `hostProc` to convert in again, and the notches are negated for it
      // to negate back.
      //
      // Sent rather than posted: a WM_TIMER callback is already on the UI
      // thread, so this reaches hostProc synchronously and stays ordered with
      // the actions around it.
      POINT screen{static_cast<LONG>(action.fromX), static_cast<LONG>(action.fromY)};
      ClientToScreen(hwnd, &screen);

      const int delta = static_cast<int>(-action.lines * WHEEL_DELTA);
      std::fprintf(stderr,
                   "BASALT_TEST_SCROLL: %.0f lines at (%.0f, %.0f)\n",
                   action.lines,
                   action.fromX,
                   action.fromY);
      SendMessage(hwnd,
                  WM_MOUSEWHEEL,
                  MAKEWPARAM(0, delta),
                  MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)));
      break;
    }

    case ScriptedInput::Kind::Focus: {
      std::fprintf(stderr, "BASALT_TEST_FOCUS: %s\n", action.text.c_str());
      if (gHost.focusManager != nullptr) {
        if (action.text == "tab") {
          gHost.focusManager->moveFocus(true);
        } else if (action.text == "shift-tab") {
          gHost.focusManager->moveFocus(false);
        } else if (action.text == "activate") {
          gHost.focusManager->activateFocused();
        } else {
          std::fprintf(stderr, "BASALT_TEST_FOCUS: unknown action\n");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      break;
    }

    case ScriptedInput::Kind::Type: {
      // Real WM_CHARs into whichever <TextInput> has focus, so what this skips
      // is the keyboard driver and nothing above it: the EDIT's own handling,
      // EN_CHANGE, the emitter, the event beat, React's re-render and the
      // controlled value coming back down all run exactly as they would.
      //
      // Focus is a precondition rather than something this arranges, which is
      // why a BASALT_TEST_TAP on the field is scheduled before it -- the same
      // pairing both other hosts use.
      const bool typed = gHost.mountingManager != nullptr &&
          gHost.mountingManager->typeIntoFocusedTextInput(action.text);
      std::fprintf(stderr,
                   "BASALT_TEST_TYPE: \"%s\"%s\n",
                   action.text.c_str(),
                   typed ? "" : " -- no field has focus");
      break;
    }
  }
}

// Queues one action a second, starting a second and a half in -- late enough
// that the first tree has been committed and mounted. Returns the delay after
// the last one, so a drag can be scheduled behind the taps.
UINT scheduleScriptedInput(const ScriptedInput &action, UINT delayMs) {
  gScriptedInput.push_back(action);
  SetTimer(gHost.window,
           kScriptedInputTimerBase + gScriptedInput.size() - 1,
           delayMs,
           fireScriptedInput);
  return delayMs + 1000;
}

// "x,y" pairs separated by ';' -- the spelling BASALT_TEST_TAP and
// BASALT_TEST_HOVER share, since a press and a hover are both just a point.
UINT scheduleTestPoints(const char *spec, UINT delayMs, ScriptedInput::Kind kind) {
  const std::string all(spec);
  size_t start = 0;
  while (start <= all.size()) {
    const size_t semicolon = all.find(';', start);
    const std::string point =
        all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
    const std::vector<double> numbers = parseNumbers(point);
    if (numbers.size() == 2) {
      delayMs = scheduleScriptedInput(
          ScriptedInput{.kind = kind, .fromX = numbers[0], .fromY = numbers[1]}, delayMs);
    }
    if (semicolon == std::string::npos) {
      break;
    }
    start = semicolon + 1;
  }
  return delayMs;
}

// BASALT_TEST_DRAG: "x1,y1,x2,y2" -- one press, twenty moves and a release, for
// the gestures a tap cannot reach. A pan is defined by the movement between
// press and release, so a tap can never exercise one.
UINT scheduleTestDrag(const char *spec, UINT delayMs) {
  const std::vector<double> numbers = parseNumbers(std::string(spec));
  if (numbers.size() != 4) {
    return delayMs;
  }
  return scheduleScriptedInput(ScriptedInput{.kind = ScriptedInput::Kind::Drag,
                                             .fromX = numbers[0],
                                             .fromY = numbers[1],
                                             .toX = numbers[2],
                                             .toY = numbers[3]},
                               delayMs);
}

// BASALT_TEST_SCROLL: "x,y,lines" triples separated by ';'. Positive lines
// scroll down, as a contentOffset does -- the same spelling the macOS host
// takes, so one variable drives a comparison across both.
UINT scheduleTestScrolls(const char *spec, UINT delayMs) {
  const std::string all(spec);
  size_t start = 0;
  while (start <= all.size()) {
    const size_t semicolon = all.find(';', start);
    const std::string step =
        all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
    const std::vector<double> numbers = parseNumbers(step);
    if (numbers.size() == 3) {
      delayMs = scheduleScriptedInput(ScriptedInput{.kind = ScriptedInput::Kind::Wheel,
                                                    .fromX = numbers[0],
                                                    .fromY = numbers[1],
                                                    .lines = numbers[2]},
                                      delayMs);
    }
    if (semicolon == std::string::npos) {
      break;
    }
    start = semicolon + 1;
  }
  return delayMs;
}

// The two escape hatches, run once, before anything is torn down.
//
// "Before anything" has to include the *window*, which is why this is not
// simply the first thing `shutdown` does. Destroying the host window destroys
// every child window with it, and a <TextInput>'s peer is one -- so a tree
// dumped after that reports every field as empty and unfocused, which reads as
// a broken text field and is a dead HWND being asked a question. WM_CLOSE is
// where nothing has been destroyed yet.
//
// Idempotent, because both callers are real: a window closed by the user or by
// BASALT_QUIT_AFTER_MS arrives through WM_CLOSE, and a run that fails before
// the window exists only reaches `shutdown`.
void captureBeforeTeardown() {
  static bool captured = false;
  if (captured) {
    return;
  }
  captured = true;
  dumpTreeIfRequested();
  snapshotIfRequested();
}

void shutdown() {
  // Before the surface stops, which tears the tree down -- and before the
  // window goes, which takes every text field's peer with it.
  captureBeforeTeardown();

  if (gHost.choreographer != nullptr) {
    gHost.choreographer->detach();
  }
  if (gHost.reactHost != nullptr) {
    // Surfaces must stop before the host goes away, or teardown asserts.
    gHost.reactHost->stopAllSurfaces();
    gHost.surfaceStarted = false;
    // Destroying the host joins the JS thread, so it has to happen while the
    // mounting manager it holds is still alive.
    gHost.reactHost.reset();
  }
  gHost.choreographer.reset();
  gHost.runLoopObserverManager.reset();
  // Before the manager it holds a raw pointer into. Nothing can reach it by
  // now -- the message loop has already returned -- but the order is the part
  // that stays true if that ever stops being so.
  gHost.touchDispatcher.reset();
  gHost.mountingManager.reset();
  gHost.target.Reset();
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

// Starts or stops React Native's error inspector.
//
// It is `LogBoxInspectorContainer`, registered by AppRegistry under the name
// "LogBox" exactly as an app registers its own component -- so this is a second
// surface rather than an overlay this host draws, and everything in it is React
// Native's own JavaScript. See core/LogBoxSurface.h.
//
// Input is retargeted rather than layered. On the other two desktops a view is
// a widget and the toolkit routes a press to whatever is on top; here there is
// nothing to route, so the dispatchers are pointed at the inspector's root
// while it is up and back at the app's when it comes down.
void hideLogBoxSurface() {
  if (gHost.reactHost == nullptr || gHost.logBoxRoot == nullptr) {
    return;
  }
  gHost.reactHost->stopSurface(kLogBoxSurfaceId);
  gHost.mountingManager->destroySurfaceRoot(kLogBoxSurfaceId);
  gHost.logBoxRoot = nullptr;
  if (gHost.touchDispatcher != nullptr) {
    gHost.touchDispatcher->setSurfaceRoot(gHost.root);
  }
  if (gHost.focusManager != nullptr) {
    gHost.focusManager->setSurfaceRoot(gHost.root);
  }
  if (gHost.window != nullptr) {
    InvalidateRect(gHost.window, nullptr, FALSE);
  }
}

void showLogBoxSurface(const std::string &appKey) {
  if (gHost.reactHost == nullptr || gHost.root == nullptr || gHost.logBoxRoot != nullptr) {
    return;
  }

  RECT client{};
  GetClientRect(gHost.window, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) {
    return;
  }

  gHost.logBoxRoot = gHost.mountingManager->createSurfaceRoot(kLogBoxSurfaceId);
  gHost.logBoxRoot->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));
  if (gHost.touchDispatcher != nullptr) {
    gHost.touchDispatcher->setSurfaceRoot(gHost.logBoxRoot);
  }
  if (gHost.focusManager != nullptr) {
    gHost.focusManager->setSurfaceRoot(gHost.logBoxRoot);
  }

  gHost.reactHost->startSurface(kLogBoxSurfaceId,
                                appKey,
                                folly::dynamic::object(),
                                constraintsFor(width, height),
                                layoutContextFor(gHost.scaleFactor));
  InvalidateRect(gHost.window, nullptr, FALSE);
}

LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    // The title bar. Each of these is answered only while an app has asked for
    // a hidden one, and falls through to Windows otherwise; see
    // win32/Win32TitleBar.h.
    case WM_NCCALCSIZE: {
      LRESULT result = 0;
      if (basalt::titleBar().handleNcCalcSize(wparam, lparam, result)) {
        return result;
      }
      break;
    }

    case WM_NCHITTEST: {
      LRESULT result = 0;
      if (basalt::titleBar().handleNcHitTest(lparam, gHost.root, result)) {
        return result;
      }
      break;
    }

    case WM_NCMOUSEMOVE:
    case WM_NCMOUSELEAVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCLBUTTONUP: {
      LRESULT result = 0;
      if (basalt::titleBar().handleNcMouse(message, wparam, result)) {
        return result;
      }
      break;
    }

    // An inactive window's caption buttons are drawn dimmer, as Windows' own are.
    case WM_ACTIVATE:
      basalt::titleBar().handleActivate(LOWORD(wparam) != WA_INACTIVE);
      break;

    // Light or dark, changed in Settings while the app runs. Windows announces
    // it with this message and this string, and nothing on this host listened
    // for it -- so neither the title bar nor Appearance ever changed.
    case WM_SETTINGCHANGE:
      if (lparam != 0 &&
          std::wcscmp(reinterpret_cast<const wchar_t *>(lparam), L"ImmersiveColorSet") == 0) {
        basalt::notifyColorSchemeChanged();
      }
      break;

    case WM_SIZE: {
      const int width = LOWORD(lparam);
      const int height = HIWORD(lparam);
      if (gHost.target) {
        gHost.target->Resize(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)));
      }
      // The surface has to be told, or the root keeps the size it started with
      // while the window changes around it.
      if (gHost.surfaceStarted && width > 0 && height > 0) {
        gHost.root->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));
        gHost.reactHost->setSurfaceConstraints(
            kSurfaceId, constraintsFor(width, height), layoutContextFor(gHost.scaleFactor));
        // The inspector covers the window, so it resizes with it.
        if (gHost.logBoxRoot != nullptr) {
          gHost.logBoxRoot->setFrame(0, 0, static_cast<float>(width), static_cast<float>(height));
          gHost.reactHost->setSurfaceConstraints(
              kLogBoxSurfaceId, constraintsFor(width, height), layoutContextFor(gHost.scaleFactor));
        }
      }
      // A maximise moves a hidden title bar's caption, and nothing else says so.
      basalt::titleBar().refreshMetrics();
      return 0;
    }

    case WM_DPICHANGED: {
      // Yoga rounds to physical pixels, so telling it the wrong factor puts
      // half-pixel seams between adjacent views. The high word is the new DPI;
      // 96 is one.
      gHost.scaleFactor = static_cast<int>(HIWORD(wparam) / 96);
      if (gHost.scaleFactor < 1) {
        gHost.scaleFactor = 1;
      }
      // Windows also hands back the rectangle the window should move to, and
      // ignoring it leaves the window the wrong size on the new monitor.
      if (const auto *suggested = reinterpret_cast<const RECT *>(lparam)) {
        SetWindowPos(hwnd,
                     nullptr,
                     suggested->left,
                     suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      // The caption is sized in device-independent pixels.
      basalt::titleBar().refreshMetrics();
      return 0;
    }

    // Mouse coordinates arrive in client-area pixels, which are the surface
    // root's own coordinates: WM_SIZE sizes the root to the client rectangle,
    // so the two spaces are the same one and nothing has to be converted.
    case WM_LBUTTONDOWN: {
      if (gHost.touchDispatcher == nullptr) {
        break;
      }
      // Capture, or a drag that leaves the window stops being reported and the
      // release never arrives -- which leaves the responder system believing a
      // finger is still down and swallows every press after it. AppKit and GTK
      // route a drag back to the view that took the press for free; here it has
      // to be asked for.
      SetCapture(hwnd);
      gHost.touchDispatcher->dispatchTouchStart(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    }

    case WM_MOUSEMOVE:
      basalt::titleBar().clearHover();
      if (gHost.touchDispatcher != nullptr) {
        // The touch model has no place for motion with no button down, and the
        // dispatcher drops it; the check here is only to keep an idle mouse
        // from walking the view tree sixty times a second.
        if (gHost.touchDispatcher->isDown()) {
          gHost.touchDispatcher->dispatchTouchMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        } else {
          // Hover, which is a different question about the same message. Views
          // that listen for none of it cost a hit test and nothing more; see
          // core/HoverTracker.h.
          gHost.touchDispatcher->dispatchHover(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        }
      }
      // Without this the cursor leaving the window is silent, and whatever it
      // was over stays hovered for good. GTK's motion controller and AppKit's
      // tracking area both report an exit on their own; Windows has to be asked,
      // once per entry, and disarms itself when it fires.
      if (!gHost.trackingMouseLeave) {
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hwnd;
        gHost.trackingMouseLeave = TrackMouseEvent(&tracking) != FALSE;
      }
      return 0;

    // Tab, Enter and space. A <TextInput>'s peer is a real child window and
    // takes its keys directly, so anything reaching here is meant for the
    // painted views -- which have no window and so no focus of Windows' own.
    case WM_KEYDOWN:
      if (gHost.focusManager != nullptr &&
          gHost.focusManager->handleKeyDown(static_cast<unsigned int>(wparam))) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      break;

    case WM_MOUSELEAVE:
      gHost.trackingMouseLeave = false;
      basalt::titleBar().clearHover();
      if (gHost.touchDispatcher != nullptr) {
        gHost.touchDispatcher->dispatchHoverLeave();
      }
      return 0;

    case WM_LBUTTONUP: {
      if (gHost.touchDispatcher == nullptr) {
        break;
      }
      // The end first: ReleaseCapture sends WM_CAPTURECHANGED synchronously,
      // and that is a cancel. Releasing first would turn every ordinary click
      // into a cancelled touch, which is a press that never fires.
      gHost.touchDispatcher->dispatchTouchEnd(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (GetCapture() == hwnd) {
        ReleaseCapture();
      }
      return 0;
    }

    // The wheel. Unlike every other mouse message, its coordinates are in
    // *screen* space -- the message is sent to the focused window rather than
    // to the one under the pointer, so a client-relative position would be
    // meaningless. Forgetting ScreenToClient gives a wheel that scrolls the
    // wrong list, or nothing, depending on where the window is on the desktop.
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      if (gHost.mountingManager == nullptr || gHost.root == nullptr) {
        break;
      }
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &point);

      const double notches =
          static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<double>(WHEEL_DELTA);
      const double step = notches * basalt::Win32ScrollViewManager::kWheelStepPixels;

      // Both axes are inverted against React Native's, and for different
      // reasons. A positive WM_MOUSEWHEEL delta is a turn away from the user,
      // which moves content down and so *reduces* contentOffset; a positive
      // WM_MOUSEHWHEEL delta is a tilt to the right, which is the direction
      // contentOffset already grows in. Getting either wrong gives a list that
      // scrolls backwards, which is the kind of bug nobody reports as a bug.
      const double dx = message == WM_MOUSEHWHEEL ? step : 0.0;
      const double dy = message == WM_MOUSEWHEEL ? -step : 0.0;

      if (gHost.mountingManager->scrollAt(gHost.root, point.x, point.y, dx, dy)) {
        // Nothing else asks for this: a scroll changes no view's frame, so no
        // transaction is mounted and setOnDidMount never fires.
        syncPeersAndRepaint();
        return 0;
      }
      break;
    }

    // A <TextInput>'s peer is a real EDIT control, and an EDIT reports to its
    // parent rather than to itself. EN_CHANGE, EN_SETFOCUS and EN_KILLFOCUS all
    // arrive here, which is why the mounting manager has a method for them at
    // all: on GTK and AppKit the widget is the view and the toolkit routes this
    // without a host in the middle.
    case WM_COMMAND:
      // A field taking focus is the other half of the focus rule: the peer is a
      // real window and holds real Win32 focus, so whichever painted view was
      // wearing the ring has to give it up. Only one of the two kinds of focus
      // can be true at a time.
      if (HIWORD(wparam) == EN_SETFOCUS && gHost.focusManager != nullptr) {
        gHost.focusManager->textInputTookFocus();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      if (gHost.mountingManager != nullptr &&
          gHost.mountingManager->handleControlCommand(wparam, lparam)) {
        return 0;
      }
      break;

    // And an EDIT asks its parent what colours to use. Without an answer it
    // draws in the system's, which have nothing to do with the `style` the
    // component was given -- on a dark field that is dark text on dark.
    // WM_CTLCOLORSTATIC as well, because a read-only EDIT asks with that one.
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      if (gHost.mountingManager != nullptr) {
        if (HBRUSH brush = gHost.mountingManager->controlColor(
                reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam))) {
          return reinterpret_cast<LRESULT>(brush);
        }
      }
      break;

    case WM_CAPTURECHANGED:
      // Capture taken away by something else -- a modal dialog, Alt+Tab, the
      // debugger. The release will never arrive, so the touch has to be
      // cancelled here or the responder system waits for a finger that is gone.
      if (gHost.touchDispatcher != nullptr) {
        gHost.touchDispatcher->dispatchTouchCancel();
      }
      return 0;

    case WM_TIMER:
      if (wparam == kSecondTreeTimer) {
        KillTimer(hwnd, kSecondTreeTimer);
        std::fprintf(stderr, "--- committing tree 2 from JS ---\n");
        callRenderFunction(2);
      }
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd, &paint);
      if (ensureTarget() && gHost.root != nullptr) {
        gHost.target->BeginDraw();
        gHost.target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        gHost.target->SetTransform(D2D1::Matrix3x2F::Identity());
        gHost.root->paint(gHost.target.Get());
        // The error inspector, over the app. A second surface rather than
        // anything this host draws; see core/LogBoxSurface.h.
        if (gHost.logBoxRoot != nullptr) {
          gHost.target->SetTransform(D2D1::Matrix3x2F::Identity());
          gHost.logBoxRoot->paint(gHost.target.Get());
        }
        // A hidden title bar's caption buttons, over the app's own content.
        basalt::titleBar().paintButtons(gHost.target.Get());
        // A lost device is reported here and nowhere else. Dropping the target
        // is the whole recovery: the next WM_PAINT rebuilds it.
        if (gHost.target->EndDraw() == D2DERR_RECREATE_TARGET) {
          gHost.target.Reset();
        }
      }
      EndPaint(hwnd, &paint);
      return 0;
    }

    // Before DestroyWindow, which takes every <TextInput>'s peer with it. See
    // captureBeforeTeardown.
    case WM_CLOSE:
      captureBeforeTeardown();
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

bool createWindow() {
  const HINSTANCE instance = GetModuleHandle(nullptr);
  WNDCLASSEX windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = hostProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.lpszClassName = L"BasaltHost";
  if (RegisterClassEx(&windowClass) == 0) {
    return false;
  }

  // WS_CLIPCHILDREN so that the Direct2D paint excludes any <TextInput> peer.
  // Without it the whole client area is painted and every EDIT child then
  // repaints itself on top, which is a visible flicker on every mount.
  constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

  RECT wanted{0, 0, kInitialWidth, kInitialHeight};
  AdjustWindowRect(&wanted, kWindowStyle, FALSE);

  gHost.window = CreateWindowEx(0,
                                windowClass.lpszClassName,
                                L"react-native-basalt \x2014 Windows",
                                kWindowStyle,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                wanted.right - wanted.left,
                                wanted.bottom - wanted.top,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
  return gHost.window != nullptr;
}

} // namespace

int main(int argc, char **argv) {
  gHost.bundlePath = argc > 1 ? argv[1] : "build/main.jsbundle.js";
  // Defaults to the raw-Fabric script, because a React screen needs
  // <ScrollView> and <TextInput> that Windows does not mount yet and would
  // render with holes in it. A React app that stays inside <View>, <Text>,
  // <Image> and <Pressable> -- js/press.js, say -- runs from here.
  gHost.moduleName = argc > 2 ? argv[2] : "";
  // Metro's entry, as the GTK and AppKit hosts read it: BASALT_DEV_ENTRY, which
  // is what `run-windows` sets, and "index" otherwise. A third argument still
  // wins for anyone passing one. This used to read the argument alone, which
  // nothing passes, so sourcePath was empty in every development run --
  // DevServerHelper then builds no URL at all ("Failed to download JS bundle
  // from Url: ."), and loadScript quietly fell back to the on-disk bundle: the
  // window opened, and no edit ever reached it.
  if (argc > 3) {
    gHost.sourcePath = argv[3];
  } else if (const char *entry = std::getenv("BASALT_DEV_ENTRY")) {
    gHost.sourcePath = entry;
  } else {
    gHost.sourcePath = "index";
  }

  // A URL the desktop launched this with, for `Linking.getInitialURL()`. Any
  // argument after the bundle and the module that carries a scheme: a shell
  // association passes one, and it does not know what came before. `://` rather
  // than a bare colon, so a Windows path like `C:\bundle.js` is not mistaken
  // for one.
  for (int i = 1; i < argc; i++) {
    const std::string argument(argv[i]);
    if (argument.find("://") != std::string::npos) {
      basalt::setInitialUrl(argument);
      break;
    }
  }

  // Per-monitor DPI, declared in code rather than in a manifest so that running
  // the binary straight out of the build directory behaves the same as running
  // an installed one. Without it Windows scales the window's bitmap and every
  // line goes soft on a high-DPI display.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // COM, on the thread that will outlive every other one.
  //
  // WIC decodes an <Image> on a worker, and the IWICBitmap it produces is
  // released here when the view holding it is deleted. A release in a thread
  // with no apartment at all is what that used to be, and the process exited
  // 0xC0000005 every time an app rendered an image. ShellExecute wants an
  // initialised thread too, and that is how core/PlatformServices opens a URL.
  //
  // Apartment-threaded because this thread owns a window and pumps messages,
  // which is what an STA is for.
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  // Before anything posts: this is what makes postToUiThread marshal rather
  // than run inline, and the mounting manager depends on it. See
  // win32/Win32UiThread.h.
  basalt::installUiThread();

  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               gHost.d2dFactory.GetAddressOf()))) {
    std::fprintf(stderr, "could not create a Direct2D factory\n");
    return 1;
  }
  if (!createWindow()) {
    std::fprintf(stderr, "could not create the window\n");
    return 1;
  }

  // The app's Expo config, if it is an Expo app and has been bundled. Before the
  // runtime is installed, as the other two hosts load it -- this one never did,
  // so Constants.expoConfig was null on Windows. It also names the window.
  basalt::loadExpoAppConfigBeside(gHost.bundlePath);
  {
    const std::string appName = basalt::expoAppName();
    basalt::titleBar().setDefaultTitle(
        !appName.empty() ? appName
                         : (!gHost.moduleName.empty() ? gHost.moduleName : "react-native-basalt"));
  }
  // Dark or light to match the app, and again whenever that changes -- in
  // Settings, or through Appearance.setColorScheme on the JavaScript thread, so
  // the change is marshalled here from wherever it was announced.
  basalt::titleBar().setDarkMode(basalt::effectiveColorScheme() == basalt::ColorScheme::Dark);
  basalt::titleBar().attach(gHost.window);
  basalt::addColorSchemeObserver([](basalt::ColorScheme scheme) {
    basalt::postToUiThread(
        [scheme] { basalt::titleBar().setDarkMode(scheme == basalt::ColorScheme::Dark); });
  });

  UINT dpi = GetDpiForWindow(gHost.window);
  gHost.scaleFactor = dpi >= 96 ? static_cast<int>(dpi / 96) : 1;

  // Bridgeless. React Native's C++ host is the bridgeless one, and there is no
  // bridge here to be the alternative -- so it asserts that.
  facebook::react::ReactNativeFeatureFlags::override(
      std::make_unique<DesktopFeatureFlags>());

  gHost.mountingManager = std::make_shared<basalt::Win32MountingManager>();
  gHost.runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
  gHost.choreographer = std::make_shared<basalt::Win32AnimationChoreographer>();

  // Every mounted transaction has to reach the screen. Windows repaints on
  // demand, so this is the difference between mutations arriving and anything
  // being visible.
  gHost.mountingManager->setOnDidMount(syncPeersAndRepaint);

  // Every <TextInput>'s EDIT peer is a child of this window. Set before the
  // first transaction, because a field that mounts without one gets no control
  // and no second chance -- `update` only creates on first sight.
  gHost.mountingManager->setHostWindow(gHost.window);

  // Light or dark, and any change to it. Before ReactHost, so the module can
  // answer from the first query.
  basalt::startObservingColorScheme();

  ReactInstanceConfig config;
  // The appId is how this host tells Metro which platform it is; see
  // APP_ID_PREFIX in packages/react-native-basalt/metro-config.js.
  config.appId = "basalt-windows";
  config.deviceName = "windows";

  // Dev mode changes three things at once: loadScript tries Metro before the
  // on-disk bundle; DevServerHelper exists, which is the only condition under
  // which ReactCxxTurboModuleProvider serves the DevSettings module a __DEV__
  // bundle requires; and ReactHost opens a packager connection whose reload
  // message reloads the instance.
  config.enableDevMode = std::getenv("BASALT_DEV") != nullptr;
  config.enableInspector = config.enableDevMode;
  if (const char *devHost = std::getenv("BASALT_DEV_HOST")) {
    config.devServerHost = devHost;
  }
  if (const char *devPort = std::getenv("BASALT_DEV_PORT")) {
    config.devServerPort = static_cast<uint32_t>(std::strtoul(devPort, nullptr, 10));
  }
  // So the http client can tell the bundle request apart from an app's own
  // fetch, and refuse to feed a Metro error page to the JS engine. See
  // core/DevBundle.h.
  if (config.enableDevMode) {
    basalt::setDevServerOrigin(config.devServerHost, config.devServerPort);
  }

  try {
    gHost.reactHost = std::make_unique<ReactHost>(
        config,
        gHost.mountingManager,
        gHost.runLoopObserverManager,
        makeContextContainer(),
        facebook::react::getDefaultOnJsErrorFunc(),
        logToConsole,
        nullptr,
        makeTurboModuleProviders(
            basalt::scriptURLFor(gHost.bundlePath,
                                 config.enableDevMode,
                                 config.devServerHost,
                                 config.devServerPort,
                                 gHost.sourcePath.empty() ? "index" : gHost.sourcePath,
                                 "windows"),
            config.enableDevMode),
        // React Native's error inspector. ReactCxxPlatform implements the
        // LogBox TurboModule itself and only provides it when a host hands over
        // one of these; passing null, as this did, left NativeLogBox.show() a
        // call into nothing. See core/LogBoxSurface.h.
        std::make_shared<basalt::LogBoxSurfaceDelegate>(
            [](const std::string &appKey) { showLogBoxSurface(appKey); },
            []() { hideLogBoxSurface(); }),
        // `useNativeDriver: true`. ReactCommon has a C++ implementation of the
        // whole animated graph and ReactCxxPlatform provides the module for it,
        // but only when a host asks by handing over a provider. Without it every
        // native-driven Animated call throws "Native animated module is not
        // available".
        std::make_shared<facebook::react::NativeAnimatedNodesManagerProvider>(),
        installBindings,
        gHost.choreographer);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "could not construct ReactHost: %s\n", error.what());
    return 1;
  }

  // How anything in the core reaches the scheduler, which only the host has.
  // Reanimated is the one caller; see core/UIManagerAccess.h.
  basalt::setEventListenerInstaller(
      [](std::shared_ptr<const facebook::react::EventListener> listener) {
        if (gHost.reactHost == nullptr) {
          return;
        }
        gHost.reactHost->runOnScheduler(
            [listener = std::move(listener)](facebook::react::Scheduler &scheduler) {
              scheduler.addEventListener(listener);
            });
      });

  // The host owns the root: Fabric never emits a Create for it.
  gHost.root = gHost.mountingManager->createSurfaceRoot(kSurfaceId);
  gHost.root->setFrame(0, 0, kInitialWidth, kInitialHeight);

  // Before the script runs, so that a press arriving during the first commit
  // has somewhere to go. The dispatcher holds the root, not a surface, so it
  // outlives every transaction mounted into it.
  gHost.touchDispatcher = std::make_unique<basalt::Win32TouchDispatcher>(
      gHost.mountingManager.get(), gHost.root);
  // The keyboard half: Tab reaching a <Pressable>, and Enter activating it.
  // All of it is this project's -- a React Native view is not a window here, so
  // there is nothing for Windows to focus. See Win32Focus.h.
  gHost.focusManager =
      std::make_unique<basalt::Win32FocusManager>(gHost.mountingManager.get(), gHost.root);

  // `loadScript` falls back to the on-disk bundle whenever the Metro fetch
  // fails, which is right when nothing is listening and wrong when Metro
  // answered with an error: running the last bundle that built, while the
  // developer looks at source that is not what is executing, hides the very
  // thing they need to see. So the fallback is allowed only for the first case,
  // and the second stops here with Metro's own message.
  const bool loaded = gHost.reactHost->loadScript(gHost.bundlePath, gHost.sourcePath);
  if (const auto devError = basalt::devBundleError()) {
    std::fprintf(stderr,
                 "Metro could not build the bundle (HTTP %ld):\n%s\n",
                 static_cast<long>(devError->status),
                 devError->message.c_str());
    // Through shutdown() rather than straight out: the fallback bundle is
    // already running on the JS thread, and dropping the host without joining
    // it aborts in a destructor instead of exiting.
    shutdown();
    return 1;
  }
  if (!loaded) {
    std::fprintf(stderr, "could not load script: %s\n", gHost.bundlePath.c_str());
    shutdown();
    return 1;
  }
  std::fprintf(stderr, "loaded script: %s\n", gHost.bundlePath.c_str());

  // The module name decides who drives the surface. Non-empty:
  // SurfaceHandler::start calls AppRegistry.runApplication. Empty: the surface
  // is registered without JS being called at all, leaving it for a script to
  // commit into through nativeFabricUIManager by hand.
  gHost.reactHost->startSurface(kSurfaceId,
                                gHost.moduleName,
                                folly::dynamic::object(),
                                constraintsFor(kInitialWidth, kInitialHeight),
                                layoutContextFor(gHost.scaleFactor));
  gHost.surfaceStarted = true;
  std::fprintf(stderr,
               "started surface %d%s%s\n",
               static_cast<int>(kSurfaceId),
               gHost.moduleName.empty() ? " (no module; raw Fabric script)" : " for module ",
               gHost.moduleName.c_str());

  ShowWindow(gHost.window, SW_SHOWNORMAL);
  UpdateWindow(gHost.window);

  if (gHost.moduleName.empty()) {
    std::fprintf(stderr, "--- committing tree 1 from JS ---\n");
    callRenderFunction(1);
    SetTimer(gHost.window, kSecondTreeTimer, 2000, nullptr);
  }

  UINT scriptedDelayMs = 1500;
  if (const char *taps = std::getenv("BASALT_TEST_TAP")) {
    scriptedDelayMs = scheduleTestPoints(taps, scriptedDelayMs, ScriptedInput::Kind::Tap);
  }
  // BASALT_TEST_HOVER: the pointer moving with no button down. A negative point
  // means it left the window, which is what WM_MOUSELEAVE reports.
  if (const char *hovers = std::getenv("BASALT_TEST_HOVER")) {
    scriptedDelayMs = scheduleTestPoints(hovers, scriptedDelayMs, ScriptedInput::Kind::Hover);
  }
  if (const char *drag = std::getenv("BASALT_TEST_DRAG")) {
    scriptedDelayMs = scheduleTestDrag(drag, scriptedDelayMs);
  }
  if (const char *scrolls = std::getenv("BASALT_TEST_SCROLL")) {
    scriptedDelayMs = scheduleTestScrolls(scrolls, scriptedDelayMs);
  }
  // BASALT_TEST_FOCUS: keyboard focus actions separated by ';' -- `tab`,
  // `shift-tab` and `activate`. The same reason the other instruments exist: a
  // real Tab needs a window the system considers focused, which an automated
  // run does not reliably have.
  if (const char *focus = std::getenv("BASALT_TEST_FOCUS")) {
    const std::string all(focus);
    size_t start = 0;
    while (start <= all.size()) {
      const size_t semicolon = all.find(';', start);
      const std::string action =
          all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
      if (!action.empty()) {
        scriptedDelayMs = scheduleScriptedInput(
            ScriptedInput{.kind = ScriptedInput::Kind::Focus, .text = action}, scriptedDelayMs);
      }
      if (semicolon == std::string::npos) {
        break;
      }
      start = semicolon + 1;
    }
  }

  // BASALT_TEST_TYPE: text for whichever field has focus. Last, so that a tap
  // scheduled above has already put focus somewhere.
  if (const char *text = std::getenv("BASALT_TEST_TYPE")) {
    scriptedDelayMs = scheduleScriptedInput(
        ScriptedInput{.kind = ScriptedInput::Kind::Type, .text = text}, scriptedDelayMs);
  }

  // BASALT_QUIT_AFTER_MS, so an automated run terminates without anyone
  // clicking anything. The same escape hatch both other hosts have.
  if (const char *quitAfter = std::getenv("BASALT_QUIT_AFTER_MS")) {
    const UINT delay = static_cast<UINT>(std::strtoul(quitAfter, nullptr, 10));
    SetTimer(
        gHost.window, 101, delay, [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
          KillTimer(hwnd, id);
          PostMessage(hwnd, WM_CLOSE, 0, 0);
        });
  }

  // Not a plain GetMessage loop: the event beat has to be induced each time the
  // queue empties, and Win32 has no "about to wait" hook to install one into.
  // See win32/Win32RunLoopObserver.h.
  const int exitCode = basalt::runMessageLoopWithBeat(gHost.runLoopObserverManager);

  shutdown();
  // After shutdown, which is where the last WIC bitmap is released.
  CoUninitialize();
  return exitCode;
}

// What is missing is now two components rather than the whole of input:
// <ScrollView> and <TextInput>. Both other hosts have them, and an ordinary
// React screen renders with holes in it without them, which is why this host
// still defaults to the raw-Fabric script rather than to a React app.
//
// Keyboard input is no longer among them. A <TextInput>'s peer is a real child
// window and takes its own keys; everything else is reached by Tab through
// Win32Focus.h, which owns the whole chain because a React Native view here is
// not a window and so has no focus of Windows' own.
