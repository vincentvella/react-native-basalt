/**
 * `setUpDeveloperTools`, for a desktop React Native platform.
 *
 * React Native's own file, with one thing added: a listener that lets the host
 * drive the developer menu.
 *
 * Of the three things that menu does, two need nothing from here.
 * `toggleElementInspector` is React Native's own device event and
 * `AppContainer-dev.js` already listens for it; `openDebugger` is a method on
 * `ReactHost`. Reload is the odd one out, and the reason this override exists:
 * it lives on `ReactHost::reloadReactInstance`, which is private, and the only
 * thing wired to it is the callback React Native's `DevSettings` module holds.
 * So the host asks JavaScript to make the call, and this is what hears it.
 *
 * Why here rather than in a module an app imports: this file runs from
 * `InitializeCore` in every development bundle, before any application code,
 * and an app that imports nothing from `react-native-basalt` still gets a
 * working dev menu. That is also why it is a copy of upstream plus a listener
 * rather than something of our own -- the file has to keep doing its own job.
 *
 * The upstream half is deliberately unmodified. When it changes, this should be
 * recopied rather than merged: everything below the marker is ours and
 * everything above it is React Native's.
 *
 * @format
 */

import Platform from 'react-native-basalt/upstream/Libraries/Utilities/Platform';

declare var console: {[string]: $FlowFixMe};

/**
 * Sets up developer tools for React Native.
 * You can use this module directly, or just require InitializeCore.
 */
if (__DEV__) {
  if (!Platform.isTesting) {
    const HMRClient =
      require('react-native-basalt/upstream/Libraries/Utilities/HMRClient').default;

    // TODO(T214991636): Remove legacy Metro log forwarding
    if (console._isPolyfilled) {
      // We assume full control over the console and send JavaScript logs to Metro.
      (
        [
          'trace',
          'info',
          'warn',
          'error',
          'log',
          'group',
          'groupCollapsed',
          'groupEnd',
          'debug',
        ]
      ).forEach(level => {
        const originalFunction = console[level];
        console[level] = function (...args) {
          HMRClient.log(level, args);
          originalFunction.apply(console, args);
        };
      });
    }
  }

  require('react-native-basalt/upstream/Libraries/Core/setUpReactRefresh');

  global[`${global.__METRO_GLOBAL_PREFIX__ ?? ''}__loadBundleAsync`] =
    require('react-native-basalt/upstream/Libraries/Core/Devtools/loadBundleFromServer').default;

  // --- react-native-basalt -------------------------------------------------
  //
  // The host's half of the developer menu. `basaltDevMenu` and `reload` are
  // spelled in native/core/DevMenu.h, which is the only other place they
  // appear.
  const RCTDeviceEventEmitter =
    require('react-native-basalt/upstream/Libraries/EventEmitter/RCTDeviceEventEmitter').default;
  const DevSettings =
    require('react-native-basalt/upstream/Libraries/Utilities/DevSettings').default;

  RCTDeviceEventEmitter.addListener('basaltDevMenu', action => {
    switch (action) {
      case 'reload':
        DevSettings.reload('dev menu');
        break;
      default:
        console.warn(`basalt: unknown dev menu action '${action}'`);
    }
  });
}
