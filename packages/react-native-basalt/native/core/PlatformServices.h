// The handful of things a core module needs an operating system for.
//
// One file rather than five seams, because each of these is two or three
// functions and splitting them would mean ten files saying very little. What
// they have in common is that the *module* above each is portable and only the
// last step is not: putting text on a clipboard, handing a URL to whatever
// opens it, and putting a modal dialog on screen.
//
// The fifth seam, after fonts, the text layout manager, the component registry
// and the colour scheme.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace basalt {

// --- Clipboard ---------------------------------------------------------------

// The clipboard's text, or empty if it holds none. Called on the main thread.
std::string clipboardText();
void setClipboardText(const std::string &text);

// --- Opening things ----------------------------------------------------------

// Whether anything is registered to handle this URL. A desktop cannot always
// answer honestly -- both implementations here check the scheme rather than
// asking every installed application -- so this errs towards yes.
bool canOpenUrl(const std::string &url);

// Hands the URL to the desktop. Returns false if it could not be launched.
bool openUrl(const std::string &url);

// --- Alerts ------------------------------------------------------------------

struct AlertRequest {
  std::string title;
  std::string message;
  // In React Native's order, which is also the order the callback reports.
  std::vector<std::string> buttons;
  // A `prompt`, with the text field pre-filled. Empty means a plain alert.
  bool hasTextInput{false};
  std::string defaultText;
  std::string placeholder;
};

// Shows a modal alert and calls `onButton` with the index of the button
// pressed, and the text if the alert had a field.
//
// Must not block: it is called from the JavaScript thread's mount step, and a
// modal run loop there would deadlock the runtime. Implementations marshal to
// the main thread and return at once.
using AlertCallback = std::function<void(int buttonIndex, const std::string &text)>;
void showAlert(const AlertRequest &request, AlertCallback onButton);

// --- Sharing ------------------------------------------------------------------

// What an app asked to share. React Native's `Share.share()` takes `message`,
// `url` and `title`, and marks each as belonging to one platform or the other;
// a desktop has no reason to honour that split, so all three arrive and each
// implementation uses what it can.
struct ShareRequest {
  std::string message;
  std::string url;
  std::string title;
  // `options.dialogTitle`, which names the picker on the platforms that show
  // one they did not draw themselves.
  std::string dialogTitle;
};

// What became of it. React Native's promise resolves with `sharedAction` or
// `dismissedAction` and rejects on anything else, so these are the three
// answers it can carry and no more.
enum class ShareOutcome { Shared, Dismissed, Failed };

// `message` is meaningful only for Failed, and becomes the rejection's reason.
using ShareCallback = std::function<void(ShareOutcome outcome, const std::string &message)>;

// Puts the content in front of the user and reports what they did with it.
//
// Must not block, for the same reason `showAlert` must not: it is called from
// the JavaScript thread and a modal run loop there would deadlock the runtime.
// Implementations marshal to the main thread and return at once, and the
// callback comes later -- possibly much later, because a share sheet is on
// screen until a person does something with it.
//
// Only macOS has a share service of its own. The other two go through
// core/ShareFallback.h, which builds a picker from a clipboard and a mail
// client; see its header for why that is the honest answer rather than a
// rejection.
void shareContent(const ShareRequest &request, ShareCallback onDone);

// Runs `work` on the UI thread after `milliseconds`, once.
//
// Every gesture recogniser needs this and nothing else does yet: a long press
// activates because nothing happened for half a second, and a double tap fails
// because a second tap did not arrive. Both are "wake me if this is still true
// later", which cannot be expressed by reacting to input alone.
//
// The callback runs on the thread that draws -- the same one input arrives on
// -- so what it touches needs no lock. There is no way to cancel: a recogniser
// that has moved on checks its own state when it wakes, which is simpler than
// owning a handle and correct even when the work has already started.
void postDelayed(double milliseconds, std::function<void()> work);

// Runs `work` on the UI thread, soon, in the order posted.
//
// The gesture registry is UI-thread state -- input arrives there and the
// recognisers run there -- but the module that configures it is called from
// JavaScript. Rather than lock the registry, its callers hop threads, which is
// the same rule the mounting managers keep and assert.
void postToUiThread(std::function<void()> work);

// Whether this is the thread that draws.
//
// Fabric dispatches an event from whichever thread produced it -- a touch from
// the UI thread, a layout event from the JavaScript thread -- and anything that
// answers by touching a worklet runtime has to know which it is on. That is the
// one caller today; see core/ReanimatedModule.cpp.
bool isUiThread();

} // namespace basalt
