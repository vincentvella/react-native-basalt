/**
 * `Platform` for Linux.
 *
 * React Native ships `Platform.android.js` and `Platform.ios.js`, and a
 * `Platform.js` whose entire body is `import Platform from './Platform'` -- a
 * shim that exists so deep imports keep working, and which relies on a
 * platform-specific file winning the resolution. With `--platform linux` and no
 * `Platform.linux.js` anywhere, that import resolves to the shim itself, and
 * `Platform.constants` comes out undefined. The first thing to touch it dies:
 *
 *     TypeError: Cannot read property 'isDisableAnimations' of undefined
 *
 * So this file exists, and `metro-config.js` is what puts it in the shim's
 * place.
 *
 * Constants come from `NativePlatformConstantsAndroid`. That is not a
 * placeholder: ReactCxxPlatform's PlatformConstantsModule genuinely returns
 * `PlatformConstantsAndroid`, so this is the spec the native side implements.
 *
 * @format
 */

'use strict';

import NativePlatformConstants from 'react-native/Libraries/Utilities/NativePlatformConstantsAndroid';

const Platform = {
  __constants: null,

  OS: 'linux',

  get Version() {
    return this.constants.Version;
  },

  get constants() {
    if (this.__constants == null) {
      this.__constants = NativePlatformConstants.getConstants();
    }
    return this.__constants;
  },

  get isTesting() {
    if (__DEV__) {
      return this.constants.isTesting;
    }
    return false;
  },

  get isDisableAnimations() {
    return this.constants.isDisableAnimations ?? this.isTesting;
  },

  // Neither is a thing on a desktop, and both are read unconditionally by
  // React Native's own code, so they answer rather than throw.
  get isTV() {
    return false;
  },

  get isVision() {
    return false;
  },

  // `linux` first, then `native`, then `default` -- the same shape as every
  // other platform's select, so `Platform.select({native: ..., default: ...})`
  // keeps working in shared code.
  select: spec =>
    'linux' in spec
      ? spec.linux
      : 'native' in spec
        ? spec.native
        : spec.default,
};

export default Platform;
