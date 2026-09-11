// react-native-linux — the macOS host process.
//
// Constructs a ReactHost, loads a script into Hermes, and runs one surface in
// an NSWindow. Everything on screen from here on is produced by Fabric: the
// mutation stream arrives through MacMountingManager exactly as it does on iOS
// and Android, and nothing hand-builds a ShadowViewMutation.
//
// The counterpart of gtk/main.cpp, and deliberately the same shape. The pieces
// a host has to supply, and where each comes from:
//
//   IMountingManager         MacMountingManager        (this repo)
//   RunLoopObserverManager   ReactCxxPlatform          (event beat)
//   AnimationChoreographer   MacAnimationChoreographer (this repo, display link)
//   ContextContainer         http + websocket client factories, below
//   ComponentRegistryFactory ComponentRegistryMac      (via the mounting manager)
//   FontRegistry             FontRegistryCoreText      (this repo, link-time seam)
//
// Threading: AppKit owns this thread. ReactHost spins up its own JS thread and
// every mount is marshalled back here by MacMountingManager. Nothing below
// touches a view off the main thread.
//
// What this host can render is what MacMountingManager can mount, which today
// is <View>. `js/demo.js` -- a surface driven straight through
// nativeFabricUIManager, with no React and no react-native JavaScript -- is
// therefore the bundle this runs, and is the same first light-up the GTK host
// had. A React app needs the JavaScript platform layer to answer to a name
// other than "linux", which is its own piece of work.

#import "MacAnimationChoreographer.h"
#import "MacMountingManager.h"
#import "MacRunLoopObserver.h"
#import "MacSnapshot.h"
#import "RnMacView.h"

#include "ExpoRuntime.h"
#include "PlatformConstantsModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"

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

struct Host {
  NSWindow *window{nil};
  RnMacView *root{nil};

  std::shared_ptr<rnlinux::MacMountingManager> mountingManager;
  std::shared_ptr<RunLoopObserverManager> runLoopObserverManager;
  std::shared_ptr<rnlinux::MacAnimationChoreographer> choreographer;
  std::unique_ptr<ReactHost> reactHost;

  CFRunLoopObserverRef runLoopObserver{nullptr};

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
void logToConsole(const std::string &message, unsigned int logLevel) {
  switch (logLevel) {
    case ReactNativeLogLevelFatal:
    case ReactNativeLogLevelError:
      NSLog(@"[js error] %s", message.c_str());
      break;
    case ReactNativeLogLevelWarning:
      NSLog(@"[js warn] %s", message.c_str());
      break;
    default:
      NSLog(@"[js] %s", message.c_str());
      break;
  }
}

// ---------------------------------------------------------------------------
// Layout constraints
// ---------------------------------------------------------------------------

// Minimum == maximum pins the root to the window's content box, which is what
// a full-window surface wants: the root fills, and flex children divide it.
LayoutConstraints constraintsFor(int width, int height) {
  // Fully qualified: Carbon's MacTypes.h defines a `Size` of its own -- a bare
  // `long` -- and AppKit pulls it in, so an unqualified name here resolves to
  // something that is not a size at all.
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
        NSLog(@"script defines no global %s()", kRenderFunctionName);
        return;
      }
      value.getObject(runtime).getFunction(runtime).call(
          runtime,
          facebook::jsi::Value(static_cast<int>(kSurfaceId)),
          facebook::jsi::Value(step));
    } catch (const facebook::jsi::JSError &error) {
      NSLog(@"%s() threw: %s\n%s",
            kRenderFunctionName,
            error.getMessage().c_str(),
            error.getStack().c_str());
    } catch (const std::exception &error) {
      NSLog(@"%s() failed: %s", kRenderFunctionName, error.what());
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
// it. All three come from core/ and are shared with the GTK host.
facebook::react::TurboModuleProviders makeTurboModuleProviders(std::string scriptURL) {
  facebook::react::TurboModuleProviders providers;
  providers.emplace_back(
      [scriptURL = std::move(scriptURL)](const std::string &name,
         const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == facebook::react::PlatformConstantsModule::kModuleName) {
          return std::make_shared<rnlinux::DesktopPlatformConstantsModule>(jsInvoker);
        }
        if (name == rnlinux::DesktopStatusBarModule::kModuleName) {
          return std::make_shared<rnlinux::DesktopStatusBarModule>(jsInvoker);
        }
        if (name == rnlinux::DesktopSourceCodeModule::kModuleName) {
          return std::make_shared<rnlinux::DesktopSourceCodeModule>(jsInvoker, scriptURL);
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

  // ReactHost throws without these two. Both are real implementations --
  // curl-backed http from core/HttpClient.cpp, and React Native's own
  // boost::beast websocket client -- and neither is about a toolkit, which is
  // why the macOS host gets them for free.
  contextContainer->insert(facebook::react::HttpClientFactoryKey,
                           facebook::react::getHttpClientFactory());
  contextContainer->insert(facebook::react::WebSocketClientFactoryKey,
                           facebook::react::getWebSocketClientFactory());

  // MessageQueueThreadFactoryKey is deliberately left unset: ReactHost then
  // installs MessageQueueThreadImpl, a real threaded queue.
  return contextContainer;
}

// RN_MAC_DUMP_TREE: write the view tree to a file on the way out, so an
// automated run can assert on what React actually produced rather than on a
// screenshot. The same idea as RN_LINUX_DUMP_TREE, and the prefixes should
// eventually be one; see plan/20-macos-host.md.
void dumpTreeIfRequested() {
  const char *path = getenv("RN_MAC_DUMP_TREE");
  if (path == nullptr || gHost.root == nil) {
    return;
  }
  NSError *error = nil;
  NSString *description = [gHost.root describeTree];
  if (![description writeToFile:[NSString stringWithUTF8String:path]
                     atomically:YES
                       encoding:NSUTF8StringEncoding
                          error:&error]) {
    NSLog(@"could not write %s: %@", path, error);
  } else {
    NSLog(@"wrote view tree to %s", path);
  }
}

void shutdown() {
  // Before the surface stops, which tears the tree down.
  dumpTreeIfRequested();

  if (gHost.choreographer != nullptr) {
    gHost.choreographer->detach();
  }
  // Stop feeding the beat before the manager it points at is released.
  rnlinux::removeRunLoopObserver(gHost.runLoopObserver);
  gHost.runLoopObserver = nullptr;

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
  gHost.mountingManager.reset();
}

} // namespace

// The window's delegate, and the application's. Resizing has to reach the
// surface or the root keeps the size it started with while the window changes
// around it; quitting has to run shutdown() or the JS thread is dropped rather
// than joined.
@interface RnMacHostDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate>
@end

@implementation RnMacHostDelegate

- (void)windowDidResize:(NSNotification *)notification {
  (void)notification;
  if (!gHost.surfaceStarted) {
    return;
  }
  const NSSize size = gHost.window.contentView.bounds.size;
  const int width = (int)size.width;
  const int height = (int)size.height;
  if (width <= 0 || height <= 0) {
    return;
  }
  [gHost.root setRnFrameX:0 y:0 width:width height:height];
  gHost.reactHost->setSurfaceConstraints(
      kSurfaceId, constraintsFor(width, height), layoutContextFor(gHost.scaleFactor));
}

// The backing scale factor changes when a window moves between a Retina display
// and an external one. Yoga rounds to physical pixels, so telling it the wrong
// factor puts half-pixel seams between adjacent views.
- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
  (void)notification;
  if (!gHost.surfaceStarted) {
    return;
  }
  gHost.scaleFactor = (int)gHost.window.backingScaleFactor;
  const NSSize size = gHost.window.contentView.bounds.size;
  gHost.reactHost->setSurfaceConstraints(kSurfaceId,
                                         constraintsFor((int)size.width, (int)size.height),
                                         layoutContextFor(gHost.scaleFactor));
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  shutdown();
}

@end

// A minimal main menu. Without one an unbundled binary has no Quit item and no
// Cmd-Q, which makes the host impossible to close except by killing it -- and
// killing it skips applicationWillTerminate, so the teardown path below would
// never run.
static void installMainMenu(void) {
  NSMenu *menuBar = [[NSMenu alloc] init];
  NSMenuItem *appItem = [[NSMenuItem alloc] init];
  [menuBar addItem:appItem];

  NSMenu *appMenu = [[NSMenu alloc] init];
  [appMenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = appMenu;

  NSApp.mainMenu = menuBar;
}

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    gHost.bundlePath = argc > 1 ? argv[1] : "build/main.jsbundle.js";
    // Defaults to the raw-Fabric script, because that is what this host can
    // currently render: a React app needs components macOS cannot mount yet.
    gHost.moduleName = argc > 2 ? argv[2] : "";
    const char *sourcePath = getenv("RN_MAC_DEV_ENTRY");
    gHost.sourcePath = sourcePath != nullptr ? sourcePath : "index";

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    installMainMenu();

    RnMacHostDelegate *delegate = [[RnMacHostDelegate alloc] init];
    NSApp.delegate = delegate;

    gHost.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, kInitialWidth, kInitialHeight)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    gHost.window.title = @"react-native-linux — macOS";
    gHost.window.delegate = delegate;
    [gHost.window center];
    gHost.scaleFactor = (int)gHost.window.backingScaleFactor;

    // Constructed here, on the main thread: MacMountingManager records this
    // thread and asserts that every mutation lands back on it.
    gHost.mountingManager = std::make_shared<rnlinux::MacMountingManager>();

    // Fabric emits no Create for a surface root -- the root shadow node is the
    // base of every diff, so it has to exist before the surface starts.
    gHost.root = gHost.mountingManager->createSurfaceRoot(kSurfaceId);
    [gHost.root setRnFrameX:0 y:0 width:kInitialWidth height:kInitialHeight];
    gHost.window.contentView = gHost.root;

    gHost.runLoopObserverManager = std::make_shared<RunLoopObserverManager>();
    gHost.choreographer = std::make_shared<rnlinux::MacAnimationChoreographer>();

    // Before ReactHost, so the beat is being induced from the first event on.
    gHost.runLoopObserver = rnlinux::installRunLoopObserver(gHost.runLoopObserverManager);

    // Say what this host needs rather than inheriting a default that moves.
    // HermesInstance gives the runtime a microtask queue only when
    // enableBridgelessArchitecture() is true, and React's scheduler enqueues a
    // microtask on its first render. This host is bridgeless -- there is no
    // bridge here to be the alternative -- so it asserts that.
    facebook::react::ReactNativeFeatureFlags::override(
        std::make_unique<DesktopFeatureFlags>());

    ReactInstanceConfig config;
    // The appId is how this host tells Metro which platform it is; see the
    // matching comment in gtk/main.cpp and APP_ID_PREFIX in
    // packages/react-native-linux/metro-config.js.
    config.appId = "react-native-desktop-macos";
    config.deviceName = "macos";

    // Dev mode changes three things at once: loadScript tries Metro before the
    // on-disk bundle; DevServerHelper exists, which is the only condition under
    // which ReactCxxTurboModuleProvider serves the DevSettings module a __DEV__
    // bundle requires; and ReactHost opens a packager connection whose reload
    // message reloads the instance.
    config.enableDevMode = getenv("RN_MAC_DEV") != nullptr;
    config.enableInspector = config.enableDevMode;
    if (const char *devHost = getenv("RN_MAC_DEV_HOST")) {
      config.devServerHost = devHost;
    }
    if (const char *devPort = getenv("RN_MAC_DEV_PORT")) {
      config.devServerPort = (uint32_t)strtoul(devPort, nullptr, 10);
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
          makeTurboModuleProviders(rnlinux::scriptURLFor(
              gHost.bundlePath,
              config.enableDevMode,
              config.devServerHost,
              config.devServerPort,
              gHost.sourcePath.empty() ? "index" : gHost.sourcePath)),
          nullptr,
          nullptr,
          installBindings,
          gHost.choreographer);
    } catch (const std::exception &error) {
      NSLog(@"could not construct ReactHost: %s", error.what());
      return 1;
    }

    if (!gHost.reactHost->loadScript(gHost.bundlePath, gHost.sourcePath)) {
      NSLog(@"could not load script: %s", gHost.bundlePath.c_str());
      return 1;
    }
    NSLog(@"loaded script: %s", gHost.bundlePath.c_str());

    // The module name decides who drives the surface. Non-empty:
    // SurfaceHandler::start calls AppRegistry.runApplication. Empty: the
    // surface is registered without JS being called at all, leaving it for a
    // script to commit into through nativeFabricUIManager by hand.
    gHost.reactHost->startSurface(kSurfaceId,
                                  gHost.moduleName,
                                  folly::dynamic::object(),
                                  constraintsFor(kInitialWidth, kInitialHeight),
                                  layoutContextFor(gHost.scaleFactor));
    gHost.surfaceStarted = true;
    NSLog(@"started surface %d%s%s",
          (int)kSurfaceId,
          gHost.moduleName.empty() ? " (no module; raw Fabric script)" : " for module ",
          gHost.moduleName.c_str());

    [gHost.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // The display link needs a window, which the root now has.
    gHost.choreographer->attachToView(gHost.root);

    if (gHost.moduleName.empty()) {
      NSLog(@"--- committing tree 1 from JS ---");
      callRenderFunction(1);
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)),
                     dispatch_get_main_queue(),
                     ^{
                       NSLog(@"--- committing tree 2 from JS ---");
                       callRenderFunction(2);
                     });
    }

    // RN_MAC_SNAPSHOT: render what is actually on screen to a PNG on the way
    // out. The tree dump above says what was mounted; this says what it looks
    // like, and the two fail differently -- a correct tree can still paint
    // nothing if the layer or the window is wrong.
    if (const char *snapshot = getenv("RN_MAC_SNAPSHOT")) {
      NSString *path = [NSString stringWithUTF8String:snapshot];
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)),
                     dispatch_get_main_queue(),
                     ^{
                       if (RnMacWriteSnapshot(gHost.root, path)) {
                         NSLog(@"wrote a snapshot to %@", path);
                       } else {
                         NSLog(@"could not write a snapshot to %@", path);
                       }
                     });
    }

    // RN_MAC_QUIT_AFTER_MS: quitting on a timer is the only way to exercise the
    // shutdown path in automation. The window cannot be closed from a script,
    // and killing the process skips applicationWillTerminate entirely, so
    // stopAllSurfaces and the teardown ordering below it would never run.
    if (const char *quitAfter = getenv("RN_MAC_QUIT_AFTER_MS")) {
      const long ms = strtol(quitAfter, nullptr, 10);
      if (ms > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)ms * NSEC_PER_MSEC),
                       dispatch_get_main_queue(),
                       ^{
                         NSLog(@"RN_MAC_QUIT_AFTER_MS elapsed; quitting");
                         [NSApp terminate:nil];
                       });
      }
    }

    [NSApp run];
  }
  return 0;
}
