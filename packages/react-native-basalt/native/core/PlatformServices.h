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

} // namespace basalt
