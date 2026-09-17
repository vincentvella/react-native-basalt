// The Windows half of core/WindowControl.h.
//
// The only one of the three where the coordinates already agree with React
// Native's -- top-left origin, y growing downwards -- and the only one where
// every function can be answered honestly.
//
// What it does have to be careful about is the difference between a window's
// frame and its client area. `GetWindowRect` is the frame, including the border
// and the title bar; `setWindowSize` is given the size an app wants, and an app
// asking for 900x700 means its own content. So the size is adjusted outwards by
// whatever the frame costs, which is what `AdjustWindowRectEx` is for -- and
// which a window with a hidden title bar costs nothing for, so the same call
// answers both cases.

#include "WindowControl.h"

#include <windows.h>

namespace basalt {

namespace {


HWND window() {
  HWND active = GetActiveWindow();
  return active != nullptr ? active : GetForegroundWindow();
}

} // namespace

WindowBounds windowBounds() {
  WindowBounds bounds;
  HWND target = window();
  if (target == nullptr) {
    return bounds;
  }

  RECT frame{};
  if (GetWindowRect(target, &frame)) {
    bounds.x = frame.left;
    bounds.y = frame.top;
  }
  // The client area, which is what an app laid out and what the other two
  // hosts report: a frame's size includes a border the app never drew in.
  RECT client{};
  if (GetClientRect(target, &client)) {
    bounds.width = client.right - client.left;
    bounds.height = client.bottom - client.top;
  }

  bounds.maximized = IsZoomed(target) != FALSE;

  // Full screen is not a state Windows has; it is a window with no border
  // filling a monitor, which is how every application does it. So the question
  // is asked the way the answer is made: no title bar, and exactly the size of
  // the monitor it is on.
  if (const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST)) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
      const LONG style = GetWindowLongW(target, GWL_STYLE);
      bounds.fullScreen = (style & WS_OVERLAPPEDWINDOW) == 0 &&
          frame.left == info.rcMonitor.left && frame.top == info.rcMonitor.top &&
          frame.right == info.rcMonitor.right && frame.bottom == info.rcMonitor.bottom;
    }
  }
  return bounds;
}

void setWindowSize(double width, double height) {
  HWND target = window();
  if (target == nullptr || width <= 0.0 || height <= 0.0) {
    return;
  }
  // Outwards from the client size the app asked for to the frame size Windows
  // positions. A borderless window adjusts by nothing, so this is right for
  // both title-bar styles.
  RECT desired{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  const LONG style = GetWindowLongW(target, GWL_STYLE);
  const LONG exStyle = GetWindowLongW(target, GWL_EXSTYLE);
  AdjustWindowRectEx(&desired, static_cast<DWORD>(style), FALSE, static_cast<DWORD>(exStyle));

  SetWindowPos(target,
               nullptr,
               0,
               0,
               desired.right - desired.left,
               desired.bottom - desired.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void setWindowPosition(double x, double y) {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  SetWindowPos(target,
               nullptr,
               static_cast<int>(x),
               static_cast<int>(y),
               0,
               0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void centerWindow() {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  RECT frame{};
  if (!GetWindowRect(target, &frame)) {
    return;
  }
  // The work area of the monitor the window is on, not the primary one and not
  // the whole monitor: centring into the space the taskbar leaves is what a
  // person means, and what every other application does.
  const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
    return;
  }

  const LONG width = frame.right - frame.left;
  const LONG height = frame.bottom - frame.top;
  const LONG x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
  const LONG y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
  SetWindowPos(target, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void setWindowFullScreen(bool fullScreen) {
  HWND target = window();
  if (target == nullptr) {
    return;
  }

  // Where the window was before it filled the screen, so that leaving puts it
  // back. One window, so one saved rect; see core/WindowControl.h on that
  // limit.
  static RECT restore{};
  static LONG restoreStyle = 0;

  const LONG style = GetWindowLongW(target, GWL_STYLE);
  const bool already = (style & WS_OVERLAPPEDWINDOW) == 0;
  if (already == fullScreen) {
    return;
  }

  if (fullScreen) {
    GetWindowRect(target, &restore);
    restoreStyle = style;

    const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
      return;
    }
    SetWindowLongW(target, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    // The whole monitor rather than its work area: filling the screen means
    // over the taskbar, which is the difference between this and maximising.
    SetWindowPos(target,
                 HWND_TOP,
                 info.rcMonitor.left,
                 info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left,
                 info.rcMonitor.bottom - info.rcMonitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    return;
  }

  SetWindowLongW(target, GWL_STYLE, restoreStyle != 0 ? restoreStyle : (style | WS_OVERLAPPEDWINDOW));
  SetWindowPos(target,
               nullptr,
               restore.left,
               restore.top,
               restore.right - restore.left,
               restore.bottom - restore.top,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}


} // namespace basalt
