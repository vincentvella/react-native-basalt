// The core React Native modules ReactCxxPlatform does not provide.
//
// Upstream supplies fifteen -- Animated, AppState, DeviceInfo, DevSettings,
// ImageLoader, LogBox, ExceptionsManager, the observers, Networking,
// PlatformConstants, SourceCode, WebSocket. What is here is the set a real app
// reaches for and finds missing, and it splits in two ways that matter.
//
// **How they fail.** `Clipboard` and `Vibration` are looked up with
// `getEnforcing`, so their absence *throws at import* -- an app that so much as
// imports Clipboard dies at startup with no render. The rest are looked up with
// `get`, which returns null, so React Native's JavaScript falls back or quietly
// does nothing: `Alert.alert()` was a dialog that never appeared and
// `Linking.openURL()` a link that never opened, with no error either way. The
// second kind is worse to debug and was less obviously urgent, which is why
// this file does both at once.
//
// **What they need.** Six modules, three of which touch an operating system --
// clipboard, opening a URL, showing a dialog -- and three of which answer
// rather than act. The first three go through `PlatformServices.h`; the other
// three are portable in full.
//
// One file because they are a set and each is small. Splitting them would mean
// six headers saying very little and one seam between them.

#pragma once

#include "PlatformServices.h"

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <string>

namespace basalt {

// Throws at import without this: `Clipboard` is a getEnforcing lookup.
class DesktopClipboardModule
    : public facebook::react::NativeClipboardCxxSpec<DesktopClipboardModule> {
 public:
  explicit DesktopClipboardModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeClipboardCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "Clipboard";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  facebook::jsi::Value getString(facebook::jsi::Runtime &rt);
  void setString(facebook::jsi::Runtime &rt, facebook::jsi::String content);
};

// Also a getEnforcing lookup, and meaningless on a desktop -- so the module
// exists and does nothing, which is the truthful implementation rather than a
// placeholder. A machine with no vibration motor cannot be made to buzz by
// anything this file could contain.
class DesktopVibrationModule
    : public facebook::react::NativeVibrationCxxSpec<DesktopVibrationModule> {
 public:
  explicit DesktopVibrationModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeVibrationCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "Vibration";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  void vibrate(facebook::jsi::Runtime &rt, double pattern);
  void vibrateByPattern(facebook::jsi::Runtime &rt,
                        facebook::jsi::Array pattern,
                        double repeat);
  void cancel(facebook::jsi::Runtime &rt);
};

// `Alert.alert()`, which until now did nothing at all -- a confirmation dialog
// that never appeared and never reported a choice, so the code after it simply
// never ran.
class DesktopAlertModule
    : public facebook::react::NativeAlertManagerCxxSpec<DesktopAlertModule> {
 public:
  explicit DesktopAlertModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeAlertManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "AlertManager";

  void alertWithArgs(facebook::jsi::Runtime &rt,
                     facebook::jsi::Object args,
                     facebook::jsi::Function callback);
};

// `Linking.openURL()`, which until now opened nothing.
class DesktopLinkingModule
    : public facebook::react::NativeLinkingManagerCxxSpec<DesktopLinkingModule> {
 public:
  explicit DesktopLinkingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeLinkingManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "LinkingManager";

  facebook::jsi::Value getInitialURL(facebook::jsi::Runtime &rt);
  facebook::jsi::Value canOpenURL(facebook::jsi::Runtime &rt, facebook::jsi::String url);
  facebook::jsi::Value openURL(facebook::jsi::Runtime &rt, facebook::jsi::String url);
  facebook::jsi::Value openSettings(facebook::jsi::Runtime &rt);
  void addListener(facebook::jsi::Runtime &rt, facebook::jsi::String eventName);
  void removeListeners(facebook::jsi::Runtime &rt, double count);
};

// Right-to-left layout. Portable in full: Yoga already does the work, and this
// is the switch that turns it on.
class DesktopI18nManagerModule
    : public facebook::react::NativeI18nManagerCxxSpec<DesktopI18nManagerModule> {
 public:
  explicit DesktopI18nManagerModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeI18nManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "I18nManager";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  void allowRTL(facebook::jsi::Runtime &rt, bool allowRTL);
  void forceRTL(facebook::jsi::Runtime &rt, bool forceRTL);
  void swapLeftAndRightInRTL(facebook::jsi::Runtime &rt, bool flipStyles);
};

// What assistive technology is doing, as *iOS* names it.
//
// The Android spec, `AccessibilityInfo`, is implemented below as well, and this
// is the one that gets used. `AccessibilityInfo.js` branches on
// `Platform.OS === 'android'` in eight places and a third platform takes the
// else branch every time -- which is the iOS path, calling
// `NativeAccessibilityManagerIOS`. So implementing only the Android module left
// `isScreenReaderEnabled()` throwing "NativeAccessibilityManagerIOS is not
// available", which is how this was found.
//
// The same shape as `Image`'s view config in phase 26 and `TextInput`'s render
// in phase 09: React Native branching two ways on a platform that is neither,
// and landing on iOS. Worth expecting rather than rediscovering.
//
// Each getter takes (onSuccess, onError) rather than returning.
class DesktopAccessibilityManagerModule
    : public facebook::react::NativeAccessibilityManagerCxxSpec<DesktopAccessibilityManagerModule> {
 public:
  explicit DesktopAccessibilityManagerModule(
      std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeAccessibilityManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "AccessibilityManager";

  void getCurrentBoldTextState(facebook::jsi::Runtime &rt,
                               facebook::jsi::Function onSuccess,
                               facebook::jsi::Function onError);
  void getCurrentGrayscaleState(facebook::jsi::Runtime &rt,
                                facebook::jsi::Function onSuccess,
                                facebook::jsi::Function onError);
  void getCurrentInvertColorsState(facebook::jsi::Runtime &rt,
                                   facebook::jsi::Function onSuccess,
                                   facebook::jsi::Function onError);
  void getCurrentReduceMotionState(facebook::jsi::Runtime &rt,
                                   facebook::jsi::Function onSuccess,
                                   facebook::jsi::Function onError);
  void getCurrentDarkerSystemColorsState(facebook::jsi::Runtime &rt,
                                         facebook::jsi::Function onSuccess,
                                         facebook::jsi::Function onError);
  void getCurrentPrefersCrossFadeTransitionsState(facebook::jsi::Runtime &rt,
                                                  facebook::jsi::Function onSuccess,
                                                  facebook::jsi::Function onError);
  void getCurrentReduceTransparencyState(facebook::jsi::Runtime &rt,
                                         facebook::jsi::Function onSuccess,
                                         facebook::jsi::Function onError);
  void getCurrentVoiceOverState(facebook::jsi::Runtime &rt,
                                facebook::jsi::Function onSuccess,
                                facebook::jsi::Function onError);

  void setAccessibilityContentSizeMultipliers(
      facebook::jsi::Runtime &rt,
      facebook::jsi::Object jsMultipliers);
  void setAccessibilityFocus(facebook::jsi::Runtime &rt, double reactTag);
  void announceForAccessibility(facebook::jsi::Runtime &rt, facebook::jsi::String announcement);
  void announceForAccessibilityWithOptions(facebook::jsi::Runtime &rt,
                                           facebook::jsi::String announcement,
                                           facebook::jsi::Object options);
};

// The Android spelling of the same thing. Kept because an app can reach it
// directly and because which one React Native picks is a JavaScript decision
// that could change.
class DesktopAccessibilityInfoModule
    : public facebook::react::NativeAccessibilityInfoCxxSpec<DesktopAccessibilityInfoModule> {
 public:
  explicit DesktopAccessibilityInfoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeAccessibilityInfoCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "AccessibilityInfo";

  void isReduceMotionEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isInvertColorsEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isHighTextContrastEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isTouchExplorationEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isAccessibilityServiceEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isGrayscaleEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void setAccessibilityFocus(facebook::jsi::Runtime &rt, double reactTag);
  void announceForAccessibility(facebook::jsi::Runtime &rt, facebook::jsi::String announcement);
  void getRecommendedTimeoutMillis(facebook::jsi::Runtime &rt,
                                   double mSec,
                                   facebook::jsi::Function onSuccess);
};

} // namespace basalt
