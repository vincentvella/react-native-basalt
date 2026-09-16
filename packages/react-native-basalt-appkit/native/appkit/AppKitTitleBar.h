// The window's title bar on macOS: its title, its colours, and whether the
// system draws it or the app does.
//
// The same two styles Win32TitleBar.h describes, and AppKit happens to offer
// both directly:
//
// - Native style. The window keeps its title bar, which sits *outside* the
//   content view -- so it takes nothing from the app and every metric is zero,
//   exactly as on Windows. Colours are the window's background plus a
//   transparent title bar, which is how AppKit lets a caption take an app's
//   colour at all; there is no per-element caption colour the way DWM has one.
//
// - Hidden style. NSWindowStyleMaskFullSizeContentView puts the content view
//   under the title bar, so the app's content reaches the top of the window.
//   The traffic lights stay -- the system keeps drawing them, which is the
//   macOS counterpart of the host painting caption buttons on Windows -- and
//   the metrics say how much room to leave for them.
//
// Main thread only, like the window. The BasaltWindow module posts here.

#pragma once

#import <Cocoa/Cocoa.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace basalt {

enum class TitleBarStyle { Native, Hidden };

// What JavaScript is told, in points -- which are the surface root's units on
// this host. All zero while the style is native, because the system's title bar
// is outside the content view and takes nothing from the app.
struct TitleBarMetrics {
  double height{0};
  double buttonsWidth{0};
  TitleBarStyle style{TitleBarStyle::Native};
};

class AppKitTitleBar {
 public:
  // The window to talk to. Held weakly: the title bar outlives nothing.
  void attach(NSWindow *window);

  void setTitle(std::optional<std::string> title);
  // 0xAARRGGBB as processColor produces, or nullopt for the system's own.
  void setColors(std::optional<uint32_t> background,
                 std::optional<uint32_t> text,
                 std::optional<uint32_t> border);
  void setStyle(TitleBarStyle style);

  TitleBarMetrics metrics() const;

  // Called whenever the metrics change, so JavaScript can re-lay-out. Cleared
  // by the module's destructor.
  void setMetricsListener(std::function<void(const TitleBarMetrics &)> listener);

 private:
  void apply();
  void notify();

  __weak NSWindow *window_{nil};
  std::optional<std::string> title_;
  std::optional<uint32_t> background_;
  std::optional<uint32_t> text_;
  std::optional<uint32_t> border_;
  TitleBarStyle style_{TitleBarStyle::Native};
  std::function<void(const TitleBarMetrics &)> listener_;
};

// The one per process, as the window is.
AppKitTitleBar &titleBar();

} // namespace basalt
