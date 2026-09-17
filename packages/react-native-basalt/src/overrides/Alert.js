/**
 * `Alert`, for a platform React Native's own copy has no branch for.
 *
 * The same problem `Share.js` has, in a module this project believed was
 * finished. `Alert.alert()` reads
 *
 *     if (Platform.OS === 'ios') { ... } else if (Platform.OS === 'android') { ... }
 *
 * with no `else`, so on a desktop it returns having done nothing at all -- no
 * dialog, no error, no warning. `AlertManager` was implemented in phase 32 and
 * was never once reached; js/alert.js logs "showing the alert" and then asserts
 * only that the main queue kept running, which it does whether or not anything
 * appeared. Found while implementing Share, which fails the same way for the
 * same reason.
 *
 * ## Two breaks, not one
 *
 * Fixing `Alert.js` alone changed nothing, because `RCTAlertManager.js` is one
 * of the self-importing shims and resolves to its `.android.js` sibling, which
 * calls `DialogManagerAndroid` -- a module this platform does not have -- and
 * returns. So this goes to `NativeAlertManager` directly.
 *
 * ## Which shape this uses
 *
 * The iOS one, because `AlertManager` is the iOS-shaped module: one method,
 * `alertWithArgs(args, callback)`, answering with an index. Android's is
 * `DialogManagerAndroid`, which is a different module this platform does not
 * have.
 *
 * With one difference, and it is the module's rather than this file's. React
 * Native's iOS path builds `buttons` as `[{0: 'Cancel'}, {1: 'Delete'}]` --
 * objects keyed by index -- and `DesktopAlertModule` reads `[{text: 'Cancel'}]`.
 * The second is the shape the rest of React Native uses for a button, and it is
 * the one kept here; nothing but this file calls that module.
 *
 * @format
 */

'use strict';

// `NativeAlertManager` directly, not `RCTAlertManager`. That file is one of the
// self-importing shims, so it resolves to its `.android.js` sibling -- which
// calls `DialogManagerAndroid`, a module this platform does not have, and
// returns having done nothing. It is the second break in the same chain and the
// reason the first fix alone changed nothing.
import NativeAlertManager from 'react-native-basalt/upstream/Libraries/Alert/NativeAlertManager';

export type AlertType = 'default' | 'plain-text' | 'secure-text' | 'login-password';
export type AlertButtonStyle = 'default' | 'cancel' | 'destructive';

export type AlertButton = {
  text?: string,
  onPress?: ?((value?: string) => any) | ?Function,
  isPreferred?: boolean,
  style?: AlertButtonStyle,
};

export type AlertButtons = Array<AlertButton>;

export type AlertOptions = {
  cancelable?: ?boolean,
  userInterfaceStyle?: 'unspecified' | 'light' | 'dark',
  onDismiss?: ?() => void,
};

// One place, because `alert` and `prompt` differ only in whether there is a
// field and what the buttons were given as.
function show(
  title: ?string,
  message?: ?string,
  callbackOrButtons?: ?(((text: string) => void) | AlertButtons),
  type?: ?AlertType,
  defaultValue?: string,
  options?: AlertOptions,
): void {
  const callbacks: Array<?Function> = [];
  const buttons: Array<{text: string}> = [];

  if (typeof callbackOrButtons === 'function') {
    // `Alert.prompt(title, message, callback)`: one button, and the callback is
    // what it does.
    callbacks[0] = callbackOrButtons;
    buttons.push({text: 'OK'});
  } else if (Array.isArray(callbackOrButtons)) {
    callbackOrButtons.forEach((button, index) => {
      callbacks[index] = button.onPress;
      buttons.push({text: button.text || 'OK'});
    });
  }

  if (buttons.length === 0) {
    // What `Alert.alert(title)` means everywhere: one button that dismisses it.
    buttons.push({text: 'OK'});
  }

  if (NativeAlertManager == null) {
    return;
  }

  NativeAlertManager.alertWithArgs(
    {
      title: title || '',
      message: message || undefined,
      buttons,
      type: type || undefined,
      defaultValue,
    },
    (index: number, value?: string) => {
      const callback = callbacks[index];
      if (callback) {
        callback(value);
      }
      // `options.onDismiss` is Android's, and a desktop dialog is dismissed
      // however it closes -- so it runs whichever button was pressed.
      if (options && typeof options.onDismiss === 'function') {
        options.onDismiss();
      }
    },
  );
}

class Alert {
  static alert(
    title: ?string,
    message?: ?string,
    buttons?: AlertButtons,
    options?: AlertOptions,
  ): void {
    show(title, message, buttons, 'default', undefined, options);
  }

  /**
   * A prompt with a text field.
   *
   * `login-password` is not offered: it is two fields and a layout, and
   * answering it with one would silently lose the password. `DesktopAlertModule`
   * warns and shows a plain alert if it is asked for.
   */
  static prompt(
    title: ?string,
    message?: ?string,
    callbackOrButtons?: ?(((text: string) => void) | AlertButtons),
    type?: ?AlertType = 'plain-text',
    defaultValue?: string,
    keyboardType?: string,
    options?: AlertOptions,
  ): void {
    show(title, message, callbackOrButtons, type, defaultValue, options);
  }
}

export default Alert;
