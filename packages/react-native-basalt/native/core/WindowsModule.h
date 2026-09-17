// `BasaltWindows`: opening and closing windows, from JavaScript.
//
// Three methods over core/WindowHost.h. What is worth saying is what a window
// *is* here, because it decides the shape of everything above: one window is
// one surface is one React root. See that header.
//
// Separate from `BasaltWindow`, which is the singular one next door: that
// module is about the window an app is already in -- its size, its title, its
// caption buttons -- and this one is about there being more than one of them.
// Merging them would mean one module whose methods mean different things
// depending on which half you are in.

#pragma once

#include <ReactCommon/TurboModule.h>

#include <memory>

namespace basalt {

class DesktopWindowsModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "BasaltWindows";

  explicit DesktopWindowsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopWindowsModule() override;

 private:
  // `open({component, title, width, height, props})` -> a promise of the new
  // window's id, or of null when the platform could not open one.
  //
  // A promise rather than a number, because the window is made on the UI
  // thread: this is called from the JavaScript one, and a synchronous answer
  // would mean blocking it on a window manager.
  static facebook::jsi::Value open(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);
  static facebook::jsi::Value close(facebook::jsi::Runtime &runtime,
                                    facebook::react::TurboModule &module,
                                    const facebook::jsi::Value *args,
                                    size_t count);
  // Every window that is open, main one first. For an app asking what it has,
  // and for a test asserting that a window really closed.
  static facebook::jsi::Value getWindows(facebook::jsi::Runtime &runtime,
                                         facebook::react::TurboModule &module,
                                         const facebook::jsi::Value *args,
                                         size_t count);
  static facebook::jsi::Value noop(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);
};

// The device event a window's closing arrives on. Named here so the two sides
// cannot drift.
inline constexpr const char *kWindowClosedEvent = "basaltWindowClosed";

} // namespace basalt
