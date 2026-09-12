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
// <Text> and <Image>. `js/demo.js` -- a surface driven straight through
// nativeFabricUIManager, with no React and no react-native JavaScript -- is
// therefore the bundle this runs by default, and is the same first light-up
// both other hosts had.

#include "Win32AnimationChoreographer.h"
#include "Win32MountingManager.h"
#include "Win32RunLoopObserver.h"
#include "Win32Snapshot.h"
#include "Win32Strings.h"
#include "Win32TouchDispatcher.h"
#include "Win32UiThread.h"
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

  std::string bundlePath;
  // Empty means the bundle is a raw Fabric script rather than a React app.
  std::string moduleName;
  // Metro's entry point, without the extension. Only meaningful in dev mode.
  std::string sourcePath;
  bool surfaceStarted{false};
  int scaleFactor{1};
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
        if (name == basalt::DesktopSourceCodeModule::kModuleName) {
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
    const std::string described = gHost.root->describeTree();
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
  bool isDrag{false};
  double fromX{0};
  double fromY{0};
  double toX{0};
  double toY{0};
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
  if (index >= gScriptedInput.size() || gHost.touchDispatcher == nullptr) {
    return;
  }
  const ScriptedInput &action = gScriptedInput[index];
  if (action.isDrag) {
    std::fprintf(stderr,
                 "BASALT_TEST_DRAG: (%.0f, %.0f) -> (%.0f, %.0f)\n",
                 action.fromX,
                 action.fromY,
                 action.toX,
                 action.toY);
    gHost.touchDispatcher->synthesiseDrag(
        action.fromX, action.fromY, action.toX, action.toY, 20);
  } else {
    std::fprintf(stderr, "BASALT_TEST_TAP: tapping (%.0f, %.0f)\n", action.fromX, action.fromY);
    gHost.touchDispatcher->synthesiseTap(action.fromX, action.fromY);
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

// BASALT_TEST_TAP: "x,y" pairs separated by ';'.
UINT scheduleTestTaps(const char *spec, UINT delayMs) {
  const std::string all(spec);
  size_t start = 0;
  while (start <= all.size()) {
    const size_t semicolon = all.find(';', start);
    const std::string point =
        all.substr(start, semicolon == std::string::npos ? std::string::npos : semicolon - start);
    const std::vector<double> numbers = parseNumbers(point);
    if (numbers.size() == 2) {
      delayMs = scheduleScriptedInput(
          ScriptedInput{.isDrag = false, .fromX = numbers[0], .fromY = numbers[1]}, delayMs);
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
  return scheduleScriptedInput(ScriptedInput{.isDrag = true,
                                             .fromX = numbers[0],
                                             .fromY = numbers[1],
                                             .toX = numbers[2],
                                             .toY = numbers[3]},
                               delayMs);
}

void shutdown() {
  // Before the surface stops, which tears the tree down.
  dumpTreeIfRequested();
  snapshotIfRequested();

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

LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
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
      }
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
      // Motion with no button down is hover, and the dispatcher drops it; the
      // check here is only to keep an idle mouse from walking the view tree
      // sixty times a second.
      if (gHost.touchDispatcher != nullptr && gHost.touchDispatcher->isDown()) {
        gHost.touchDispatcher->dispatchTouchMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
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
        // A lost device is reported here and nowhere else. Dropping the target
        // is the whole recovery: the next WM_PAINT rebuilds it.
        if (gHost.target->EndDraw() == D2DERR_RECREATE_TARGET) {
          gHost.target.Reset();
        }
      }
      EndPaint(hwnd, &paint);
      return 0;
    }

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

  RECT wanted{0, 0, kInitialWidth, kInitialHeight};
  AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);

  gHost.window = CreateWindowEx(0,
                                windowClass.lpszClassName,
                                L"react-native-basalt \x2014 Windows",
                                WS_OVERLAPPEDWINDOW,
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
  gHost.sourcePath = argc > 3 ? argv[3] : "";

  // Per-monitor DPI, declared in code rather than in a manifest so that running
  // the binary straight out of the build directory behaves the same as running
  // an installed one. Without it Windows scales the window's bitmap and every
  // line goes soft on a high-DPI display.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
  gHost.mountingManager->setOnDidMount(requestRepaint);

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
                                 gHost.sourcePath.empty() ? "index" : gHost.sourcePath),
            config.enableDevMode),
        nullptr,
        nullptr,
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
    scriptedDelayMs = scheduleTestTaps(taps, scriptedDelayMs);
  }
  if (const char *drag = std::getenv("BASALT_TEST_DRAG")) {
    scriptedDelayMs = scheduleTestDrag(drag, scriptedDelayMs);
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
  return exitCode;
}

// What is missing is now two components rather than the whole of input:
// <ScrollView> and <TextInput>. Both other hosts have them, and an ordinary
// React screen renders with holes in it without them, which is why this host
// still defaults to the raw-Fabric script rather than to a React app.
//
// Keyboard input is missing with <TextInput>, and for the same reason: there is
// nothing yet that focus could belong to. WM_CHAR and WM_KEYDOWN reach this
// window and stop here, which is the shape the mouse was in before
// Win32TouchDispatcher.
