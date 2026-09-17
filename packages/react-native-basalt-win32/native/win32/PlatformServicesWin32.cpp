// Windows' half of core/PlatformServices.h: the clipboard, opening a URL, an
// alert, and getting work onto the UI thread.
//
// The last of those is the one the mounting manager depends on, and it is the
// only one with a real design decision in it. See postToUiThread below.

#include "PlatformServices.h"
#include "ShareFallback.h"

#include "Win32Strings.h"
#include "Win32UiThread.h"

#include <windows.h>

#include <shellapi.h>

#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace basalt {

using win32::narrow;
using win32::widen;
namespace {

// --- the UI thread -----------------------------------------------------------
//
// A message-only window, created on whichever thread calls `installUiThread`
// first -- which the host does before it starts React Native, and which the
// tests do not do at all.
//
// A message-only window (HWND_MESSAGE) rather than posting to the host's real
// window: it exists before any surface does, it cannot be destroyed by a user
// closing something, and it keeps this seam from needing to know what the host
// draws into. It is also the mechanism a Win32 program is expected to use --
// PostMessage is the only thread-safe way into a message loop.
//
// GTK does this with g_idle_add_full and macOS with dispatch_async_f. All three
// are the same promise: the work runs on the UI thread, and work posted earlier
// runs first. That ordering is not a nicety -- a command that arrived before
// the transaction creating the view it names would find nothing and be dropped.

constexpr UINT kRunWork = WM_APP + 1;

struct UiThread {
  std::mutex mutex;
  std::vector<std::function<void()>> pending;
  HWND window = nullptr;
  std::thread::id threadId{};
};

UiThread &uiThread() {
  static UiThread instance;
  return instance;
}

void drainPendingWork() {
  std::vector<std::function<void()>> work;
  {
    std::lock_guard<std::mutex> lock(uiThread().mutex);
    work.swap(uiThread().pending);
  }
  for (auto &item : work) {
    item();
  }
}

LRESULT CALLBACK uiThreadProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == kRunWork) {
    drainPendingWork();
    return 0;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

} // namespace

// See Win32UiThread.h.

void installUiThread() {
  if (uiThread().window != nullptr) {
    return;
  }
  static const ATOM registered = [] {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = uiThreadProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"BasaltUiThreadDispatch";
    return RegisterClassExW(&windowClass);
  }();
  (void)registered;

  uiThread().threadId = std::this_thread::get_id();
  uiThread().window = CreateWindowExW(0,
                                      L"BasaltUiThreadDispatch",
                                      L"",
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      HWND_MESSAGE,
                                      nullptr,
                                      GetModuleHandleW(nullptr),
                                      nullptr);
}

bool hasUiThread() {
  return uiThread().window != nullptr;
}

bool isUiThread() {
  // With no UI thread installed there is only one thread that matters, so
  // saying yes is both true and what keeps the tests from marshalling into a
  // message loop nobody is pumping.
  if (uiThread().window == nullptr) {
    return true;
  }
  return std::this_thread::get_id() == uiThread().threadId;
}

void postToUiThread(std::function<void()> work) {
  if (!work) {
    return;
  }

  // No host, so no message loop to post to: run it here. This is the harness
  // and the test path, and it is safe precisely because there is no second
  // thread in it. A real host installs the window first, after which even work
  // posted *from* the UI thread is queued rather than run inline -- which is
  // what preserves ordering against work already in the queue, and is the same
  // rule the other two platforms follow.
  if (uiThread().window == nullptr) {
    work();
    return;
  }

  {
    std::lock_guard<std::mutex> lock(uiThread().mutex);
    uiThread().pending.push_back(std::move(work));
  }
  PostMessageW(uiThread().window, kRunWork, 0, 0);
}

void postDelayed(double milliseconds, std::function<void()> work) {
  if (!work) {
    return;
  }
  if (uiThread().window == nullptr) {
    work();
    return;
  }
  // A timer queue rather than SetTimer: SetTimer's resolution is the message
  // loop's and it coalesces, which is wrong for something a JavaScript timer is
  // waiting on. The callback arrives on a pool thread, so it is bounced onto
  // the UI thread rather than running there.
  struct Delayed {
    std::function<void()> work;
    HANDLE timer = nullptr;
  };
  auto *delayed = new Delayed{std::move(work), nullptr};
  const auto fire = [](PVOID parameter, BOOLEAN) {
    auto *item = static_cast<Delayed *>(parameter);
    postToUiThread([item] {
      item->work();
      if (item->timer != nullptr) {
        DeleteTimerQueueTimer(nullptr, item->timer, nullptr);
      }
      delete item;
    });
  };
  if (!CreateTimerQueueTimer(&delayed->timer,
                             nullptr,
                             fire,
                             delayed,
                             static_cast<DWORD>(milliseconds < 0 ? 0 : milliseconds),
                             0,
                             WT_EXECUTEONLYONCE)) {
    // Could not schedule it; running late is better than never.
    postToUiThread(std::move(delayed->work));
    delete delayed;
  }
}

// --- clipboard ---------------------------------------------------------------

namespace {

// The clipboard is one lock shared by every window on the desktop, and
// OpenClipboard does not wait for it: if a window anywhere has it open, the
// call fails at once with access denied. Clipboard listeners are windows, and
// each is told of every change and opens the clipboard to read it, so a read or
// a write that lands while one is reading is refused -- and before this, the
// refusal was silent: a write simply did not happen.
//
// Found as js/modules.js failing "clipboard takes unicode", its second write in
// a row, in 2 of 15 runs on the development machine, which compare_hosts.sh
// reported as Windows disagreeing with Linux. What was holding the clipboard
// was never caught: with this retry in place and logging on every refusal, 190
// further runs saw none at all. So the retry is not proven to be what fixed
// that; tests/test_win32_clipboard.cpp is what shows it is needed, by holding
// the clipboard from a window and failing without it.
//
// A few short tries is what everyone does, Chromium included. Sleep(5) sleeps
// a scheduler tick, about 15ms, so this is up to ~150ms on the UI thread --
// only when somebody else has the clipboard, and a listener holds it for a
// read rather than for long.
bool openClipboard() {
  constexpr int kAttempts = 10;
  constexpr DWORD kPauseMs = 5;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    if (OpenClipboard(nullptr)) {
      return true;
    }
    Sleep(kPauseMs);
  }
  return false;
}

} // namespace

std::string clipboardText() {
  if (!openClipboard()) {
    return {};
  }
  std::string result;
  // CF_UNICODETEXT rather than CF_TEXT: the ANSI variant loses anything outside
  // the active code page, and a clipboard is exactly where that shows up.
  if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto *text = static_cast<const wchar_t *>(GlobalLock(handle))) {
      result = narrow(text);
      GlobalUnlock(handle);
    }
  }
  CloseClipboard();
  return result;
}

void setClipboardText(const std::string &text) {
  if (!openClipboard()) {
    return;
  }
  EmptyClipboard();

  const std::wstring wide = widen(text);
  const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  // GMEM_MOVEABLE, and ownership passes to the clipboard on success -- freeing
  // it afterwards is a use-after-free that usually survives long enough to be
  // mysterious.
  if (const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (auto *destination = static_cast<wchar_t *>(GlobalLock(handle))) {
      memcpy(destination, wide.c_str(), bytes);
      GlobalUnlock(handle);
      if (SetClipboardData(CF_UNICODETEXT, handle) == nullptr) {
        GlobalFree(handle);
      }
    } else {
      GlobalFree(handle);
    }
  }
  CloseClipboard();
}

// --- opening a URL -----------------------------------------------------------

bool canOpenUrl(const std::string &url) {
  // Answered by asking the registry whether the scheme is registered, which is
  // what "can open" means on Windows: a scheme is a key under HKCR with a
  // URL Protocol value. Not by trying to open it, which would open it.
  const size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0) {
    return false;
  }
  const std::wstring scheme = widen(url.substr(0, colon));

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, scheme.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  const bool isProtocol =
      RegQueryValueExW(key, L"URL Protocol", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  RegCloseKey(key);
  return isProtocol;
}

bool openUrl(const std::string &url) {
  const std::wstring wide = widen(url);
  // ShellExecute returns a fake HINSTANCE; anything above 32 is success. That
  // is the documented contract, odd as it looks.
  const auto result = reinterpret_cast<INT_PTR>(
      ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return result > 32;
}

// --- sharing -----------------------------------------------------------------

// Windows does have a share UI -- the DataTransferManager's, the one the Share
// charm opens -- and reaching it means WinRT interop:
// `IDataTransferManagerInterop::ShowShareUIForWindow` against an HWND, plus the
// activation and the data-requested handler. That is worth doing and is not
// done here, because it cannot be tested from the machine this was written on
// and a share sheet that fails is worse than one that is plainly minimal. See
// plan/backlog.md.
//
// Until then, the picker core/ShareFallback.h builds out of a clipboard and a
// mail client, which is what every desktop has. Nothing here beyond the call:
// it goes through showAlert, setClipboardText and openUrl, all of which are
// implemented above.
void shareContent(const ShareRequest &request, ShareCallback onDone) {
  shareThroughFallbackPicker(request, std::move(onDone));
}

// --- menus --------------------------------------------------------------------

namespace {

// "Ctrl+R" as a menu label suffix. Windows draws an accelerator by convention
// rather than by property: the text after a tab in an item's string is
// right-aligned, and that is all an HMENU knows about shortcuts. Nothing here
// binds the key -- the host does.
std::wstring withShortcut(const std::string &label, const std::string &shortcut) {
  std::wstring text = widen(label);
  if (!shortcut.empty()) {
    text += L"\t";
    text += widen(shortcut);
  }
  return text;
}

} // namespace

void showMenu(const MenuRequest &request, MenuCallback onChosen) {
  // Onto the UI thread. TrackPopupMenuEx runs its own message loop and must be
  // called on the thread that owns the window -- and running it on the
  // JavaScript thread would deadlock the runtime, which is the same reason
  // showAlert and the GTK and AppKit menus marshal.
  postToUiThread([request, onChosen = std::move(onChosen)] {
    HWND window = GetActiveWindow();
    if (window == nullptr) {
      window = GetForegroundWindow();
    }
    if (window == nullptr) {
      onChosen(-1);
      return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
      onChosen(-1);
      return;
    }

    for (size_t i = 0; i < request.entries.size(); i++) {
      const MenuEntry &entry = request.entries[i];
      if (entry.isSeparator()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        continue;
      }
      // The command id is the index plus one: TrackPopupMenu returns zero for
      // "nothing was chosen", so zero cannot also mean the first item.
      const std::wstring text = withShortcut(entry.label, entry.shortcut);
      AppendMenuW(menu,
                  MF_STRING | (entry.enabled ? MF_ENABLED : MF_GRAYED),
                  static_cast<UINT_PTR>(i + 1),
                  text.c_str());
    }

    POINT at;
    if (request.x >= 0.0 && request.y >= 0.0) {
      // The request is in the window's client coordinates, which is where every
      // other coordinate in this host lives; TrackPopupMenuEx wants screen.
      at.x = static_cast<LONG>(request.x);
      at.y = static_cast<LONG>(request.y);
      ClientToScreen(window, &at);
    } else if (!GetCursorPos(&at)) {
      at.x = 0;
      at.y = 0;
    }

    // SetForegroundWindow before and the null PostMessage after are both from
    // the documented TrackPopupMenu recipe: without them a menu opened from a
    // window that is not foreground stays up after a click elsewhere, because
    // it never receives the message that would dismiss it.
    SetForegroundWindow(window);
    const int chosen = TrackPopupMenuEx(menu,
                                        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                                        at.x,
                                        at.y,
                                        window,
                                        nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);

    onChosen(chosen > 0 ? chosen - 1 : -1);
  });
}

// --- alerts ------------------------------------------------------------------

void showAlert(const AlertRequest &request, AlertCallback onButton) {
  // MessageBox, which means up to three buttons and no text field. React
  // Native's Alert allows more of both.
  //
  // TaskDialogIndirect would give arbitrary buttons and is the right answer;
  // it needs a comctl32 v6 manifest, which is a host concern rather than a
  // seam concern, so it waits for the host. A prompt needs a custom dialog on
  // every platform -- GTK has the same gap, per plan/backlog.md -- and is not
  // faked here.
  const std::wstring title = widen(request.title);
  const std::wstring message = widen(request.message);

  UINT type = MB_OK;
  if (request.buttons.size() == 2) {
    type = MB_OKCANCEL;
  } else if (request.buttons.size() >= 3) {
    type = MB_YESNOCANCEL;
  }

  const int pressed = MessageBoxW(nullptr, message.c_str(), title.c_str(), type);
  if (!onButton) {
    return;
  }

  // Reported as an index into the buttons the app gave, which is what React
  // Native's callback means, rather than as Windows' IDOK/IDCANCEL.
  int index = 0;
  switch (pressed) {
    case IDOK:
    case IDYES:
      index = 0;
      break;
    case IDNO:
      index = 1;
      break;
    case IDCANCEL:
      index = request.buttons.empty() ? 0 : static_cast<int>(request.buttons.size()) - 1;
      break;
    default:
      index = 0;
      break;
  }
  onButton(index, std::string{});
}

} // namespace basalt
