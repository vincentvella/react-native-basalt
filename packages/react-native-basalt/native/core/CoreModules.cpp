#include "CoreModules.h"

#include <glog/logging.h>

#include <mutex>

namespace basalt {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

namespace {

// A promise already settled. These answers are all in memory or one system call
// away, so there is nothing to wait for -- and going through JavaScript's own
// Promise.resolve needs no invoker and no thread hop.
Value resolved(Runtime &rt, Value &&value) {
  auto promise = rt.global().getPropertyAsObject(rt, "Promise");
  return promise.getPropertyAsFunction(rt, "resolve").callWithThis(rt, promise, value);
}

Value rejected(Runtime &rt, const std::string &message) {
  auto promise = rt.global().getPropertyAsObject(rt, "Promise");
  auto error = rt.global()
                   .getPropertyAsFunction(rt, "Error")
                   .callAsConstructor(rt, String::createFromUtf8(rt, message));
  return promise.getPropertyAsFunction(rt, "reject").callWithThis(rt, promise, error);
}

std::string optionalString(Runtime &rt, const Object &object, const char *key) {
  const Value value = object.getProperty(rt, key);
  return value.isString() ? value.asString(rt).utf8(rt) : std::string{};
}

} // namespace

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

Object DesktopClipboardModule::getConstants(Runtime &rt) {
  return Object(rt);
}

Value DesktopClipboardModule::getString(Runtime &rt) {
  return resolved(rt, Value(String::createFromUtf8(rt, clipboardText())));
}

void DesktopClipboardModule::setString(Runtime &rt, String content) {
  setClipboardText(content.utf8(rt));
}

// ---------------------------------------------------------------------------
// Vibration
// ---------------------------------------------------------------------------

Object DesktopVibrationModule::getConstants(Runtime &rt) {
  return Object(rt);
}

void DesktopVibrationModule::vibrate(Runtime &rt, double pattern) {
  (void)rt;
  (void)pattern;
}

void DesktopVibrationModule::vibrateByPattern(Runtime &rt, Array pattern, double repeat) {
  (void)rt;
  (void)pattern;
  (void)repeat;
}

void DesktopVibrationModule::cancel(Runtime &rt) {
  (void)rt;
}

// ---------------------------------------------------------------------------
// Alert
// ---------------------------------------------------------------------------

void DesktopAlertModule::alertWithArgs(Runtime &rt, Object args, Function callback) {
  AlertRequest request;
  request.title = optionalString(rt, args, "title");
  request.message = optionalString(rt, args, "message");

  // `buttons` is [{text}, ...] in the order Alert.alert was given them, and the
  // callback reports an index into that same list.
  const Value buttons = args.getProperty(rt, "buttons");
  if (buttons.isObject() && buttons.asObject(rt).isArray(rt)) {
    Array list = buttons.asObject(rt).asArray(rt);
    const size_t count = list.size(rt);
    for (size_t i = 0; i < count; i++) {
      Object button = list.getValueAtIndex(rt, i).asObject(rt);
      std::string text = optionalString(rt, button, "text");
      if (text.empty()) {
        text = "OK";
      }
      request.buttons.push_back(std::move(text));
    }
  }
  if (request.buttons.empty()) {
    // What Alert.alert() with no buttons means on every platform.
    request.buttons.emplace_back("OK");
  }

  // `type` is 'default' | 'plain-text' | 'secure-text' | 'login-password'. Only
  // a single text field is offered here: a login-password prompt is two fields
  // and a layout, and answering it with one would silently lose the password.
  const std::string type = optionalString(rt, args, "type");
  if (type == "plain-text" || type == "secure-text") {
    request.hasTextInput = true;
    request.defaultText = optionalString(rt, args, "defaultValue");
    request.placeholder = optionalString(rt, args, "keyboardType");
  } else if (!type.empty() && type != "default") {
    LOG(WARNING) << "Alert type '" << type << "' is not supported; showing a plain alert";
  }

  // The callback has to survive until a human presses something, and it belongs
  // to the runtime. A shared_ptr captured by the platform's completion handler
  // is what keeps it alive without keeping the runtime alive.
  auto shared = std::make_shared<Function>(std::move(callback));
  auto invoker = jsInvoker_;

  showAlert(request, [shared, invoker](int buttonIndex, const std::string &text) {
    if (invoker == nullptr) {
      return;
    }
    // Back onto the JavaScript thread: the platform calls this from its own
    // main thread, and a jsi::Function may only be called on the runtime's.
    invoker->invokeAsync([shared, buttonIndex, text](Runtime &rt) {
      // React Native's callback is (action, buttonKeyOrIndex, text) on iOS and
      // (action, buttonIndex) on Android. Both read the first two, and the
      // third only when a prompt was shown.
      shared->call(rt,
                   String::createFromUtf8(rt, "buttonClicked"),
                   Value(buttonIndex),
                   String::createFromUtf8(rt, text));
    });
  });
}

// ---------------------------------------------------------------------------
// Linking
// ---------------------------------------------------------------------------

Value DesktopLinkingModule::getInitialURL(Runtime &rt) {
  // Nothing launches a desktop host with a URL yet: there is no registered
  // scheme handler and no command-line parsing for one. Null rather than empty
  // string, which is what React Native's JavaScript checks for.
  return resolved(rt, Value::null());
}

Value DesktopLinkingModule::canOpenURL(Runtime &rt, String url) {
  return resolved(rt, Value(canOpenUrl(url.utf8(rt))));
}

Value DesktopLinkingModule::openURL(Runtime &rt, String url) {
  const std::string target = url.utf8(rt);
  if (!openUrl(target)) {
    // Rejecting rather than resolving false: that is React Native's contract,
    // and an app that awaits openURL expects a throw when it fails.
    return rejected(rt, "Unable to open URL: " + target);
  }
  return resolved(rt, Value(true));
}

Value DesktopLinkingModule::openSettings(Runtime &rt) {
  // There is no per-application settings page on a desktop to open.
  return rejected(rt, "openSettings is not supported on this platform");
}

void DesktopLinkingModule::addListener(Runtime &rt, String eventName) {
  (void)rt;
  (void)eventName;
  // Incoming URLs would arrive here. Nothing delivers them yet; see
  // getInitialURL above.
}

void DesktopLinkingModule::removeListeners(Runtime &rt, double count) {
  (void)rt;
  (void)count;
}

// ---------------------------------------------------------------------------
// I18nManager
// ---------------------------------------------------------------------------

namespace {

struct I18nState {
  std::mutex mutex;
  bool allowRTL{false};
  bool forceRTL{false};
  bool swapLeftAndRight{true};
};

I18nState &i18n() {
  static I18nState state;
  return state;
}

} // namespace

Object DesktopI18nManagerModule::getConstants(Runtime &rt) {
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);

  Object constants(rt);
  // `isRTL` is what the layout actually is. Only `forceRTL` can make it true
  // here: deciding it from the system locale would need a locale database this
  // does not have, and guessing wrong flips an app's entire layout.
  constants.setProperty(rt, "isRTL", state.forceRTL);
  constants.setProperty(rt, "doLeftAndRightSwapInRTL", state.swapLeftAndRight);
  // Empty rather than a guess. React Native's JavaScript treats it as opaque
  // and only apps read it.
  constants.setProperty(rt, "localeIdentifier", String::createFromUtf8(rt, ""));
  return constants;
}

void DesktopI18nManagerModule::allowRTL(Runtime &rt, bool allow) {
  (void)rt;
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.allowRTL = allow;
}

void DesktopI18nManagerModule::forceRTL(Runtime &rt, bool force) {
  (void)rt;
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.forceRTL = force;
  // Deliberately not restarting or relaying out. On iOS and Android this takes
  // effect after a reload, and React Native's own documentation says so -- an
  // app that flipped direction mid-session would be a surprise rather than a
  // feature.
}

void DesktopI18nManagerModule::swapLeftAndRightInRTL(Runtime &rt, bool flipStyles) {
  (void)rt;
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.swapLeftAndRight = flipStyles;
}

// ---------------------------------------------------------------------------
// AccessibilityManager -- the iOS spelling, and the one that gets used
// ---------------------------------------------------------------------------

namespace {

// Every getter is (onSuccess, onError) and every answer here is false, for the
// reason given at the Android module below.
void answerState(Runtime &rt, Function &onSuccess, bool value) {
  onSuccess.call(rt, Value(value));
}

} // namespace

void DesktopAccessibilityManagerModule::getCurrentBoldTextState(Runtime &rt,
                                                                Function onSuccess,
                                                                Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentGrayscaleState(Runtime &rt,
                                                                 Function onSuccess,
                                                                 Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentInvertColorsState(Runtime &rt,
                                                                    Function onSuccess,
                                                                    Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentReduceMotionState(Runtime &rt,
                                                                    Function onSuccess,
                                                                    Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentDarkerSystemColorsState(Runtime &rt,
                                                                          Function onSuccess,
                                                                          Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentPrefersCrossFadeTransitionsState(
    Runtime &rt,
    Function onSuccess,
    Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentReduceTransparencyState(Runtime &rt,
                                                                          Function onSuccess,
                                                                          Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentVoiceOverState(Runtime &rt,
                                                                 Function onSuccess,
                                                                 Function onError) {
  (void)onError;
  // Whether a screen reader is running. False rather than asking, for the same
  // reason as the rest: saying yes wrongly makes React Native's own components
  // announce things and alter focus order for a user who is not there.
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::setAccessibilityContentSizeMultipliers(
    Runtime &rt,
    Object jsMultipliers) {
  (void)rt;
  (void)jsMultipliers;
  // Dynamic Type's scale factors. Nothing here scales text for accessibility
  // settings, which is also why the text layer passes fontSizeMultiplier as 1.
}

void DesktopAccessibilityManagerModule::setAccessibilityFocus(Runtime &rt, double reactTag) {
  (void)rt;
  (void)reactTag;
}

void DesktopAccessibilityManagerModule::announceForAccessibility(Runtime &rt,
                                                                 String announcement) {
  (void)rt;
  (void)announcement;
}

void DesktopAccessibilityManagerModule::announceForAccessibilityWithOptions(Runtime &rt,
                                                                            String announcement,
                                                                            Object options) {
  (void)rt;
  (void)announcement;
  (void)options;
}

// ---------------------------------------------------------------------------
// AccessibilityInfo -- the Android spelling
// ---------------------------------------------------------------------------

namespace {

void answer(Runtime &rt, Function &onSuccess, bool value) {
  onSuccess.call(rt, Value(value));
}

} // namespace

// Every one of these is false, and each is false for a reason rather than as a
// placeholder.
//
// Neither desktop exposes "is a screen reader running" without asking the
// accessibility bus -- AT-SPI on Linux, and on macOS a check Apple has
// deprecated twice -- and answering *true* wrongly is worse than answering
// false: it makes React Native's own components change behaviour, announce
// things and alter focus order for a user who is not there.
void DesktopAccessibilityInfoModule::isReduceMotionEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isInvertColorsEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isHighTextContrastEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isTouchExplorationEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isAccessibilityServiceEnabled(Runtime &rt,
                                                                   Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isGrayscaleEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::setAccessibilityFocus(Runtime &rt, double reactTag) {
  (void)rt;
  (void)reactTag;
  // Moving a screen reader's cursor needs the platform's accessibility API to
  // post a focus notification for a specific view, which neither mounting
  // manager exposes by tag yet. See plan/backlog.md.
}

void DesktopAccessibilityInfoModule::announceForAccessibility(Runtime &rt, String announcement) {
  (void)rt;
  (void)announcement;
  // Same gap: an announcement is a notification posted to the accessibility
  // bus, and nothing here has a handle on it.
}

void DesktopAccessibilityInfoModule::getRecommendedTimeoutMillis(Runtime &rt,
                                                                 double mSec,
                                                                 Function onSuccess) {
  // The original timeout, unmodified. This exists so an app can be told to give
  // a user longer; nothing here knows of a reason to.
  onSuccess.call(rt, Value(mSec));
}

} // namespace basalt
