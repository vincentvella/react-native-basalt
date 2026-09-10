/**
 * Metro configuration for the `linux` platform.
 *
 * An out-of-tree React Native platform has to do two things to Metro, and the
 * second is the one that is not obvious.
 *
 * First, `linux` has to be a platform Metro knows about, so that
 * `Button.linux.js` wins over `Button.js` the way `Button.android.js` would.
 *
 * Second, some of React Native's own modules cannot work on a platform it has
 * never heard of. There are three kinds, and each needs different handling:
 *
 *   1. **Self-importing shims.** React Native ships a family of files whose
 *      entire body is `import X from './X'; export default X;`, each marked
 *      with a note about "backwards compatibility of subpath (deep) imports".
 *      They exist so `react-native/Libraries/Image/Image` keeps working, and
 *      they rely on a platform-specific sibling winning the resolution. Bundle
 *      for `linux` and each resolves to *itself*, exports undefined, and kills
 *      whatever touches it -- as `Platform.constants` being undefined, or a
 *      view config being undefined, or a component being undefined.
 *
 *   2. **Modules with no neutral fallback at all**, which do not resolve, so
 *      there is no resolution to rewrite -- only a failure to intercept.
 *
 *   3. **Modules this platform genuinely implements differently**, which is
 *      only `Platform` so far.
 *
 * React Native for Windows solves all of this with a whole override system and
 * a fork of every file it replaces. This is the same idea at the smallest size
 * that works.
 *
 * Usage, in an app's metro.config.js:
 *
 *     const {withLinuxPlatform} = require('react-native-linux/metro-config');
 *     module.exports = withLinuxPlatform(config);
 *
 * @format
 */

'use strict';

const path = require('path');

const OVERRIDE_DIR = path.join(__dirname, 'src', 'overrides');

/**
 * Kind 1: React Native's self-importing deep-import shims.
 *
 * Each is answered with its own `.android.js` sibling rather than a file of
 * ours. That is deliberate: this platform reports `PlatformConstantsAndroid`
 * from C++, shares ReactCommon's prop parsing, and drives the same components
 * Android's JavaScript drives, so Android's implementation is the one that
 * matches what is actually implemented here. Copying them would mean nine forks
 * drifting silently from upstream.
 *
 * Found with:
 *
 *     grep -rl 'backwards compatibility of subpath (deep) imports' Libraries src
 *
 * Spelled out rather than detected, so a new shim upstream produces an honest
 * failure here rather than a silent redirect to Android.
 */
const SELF_IMPORTING_SHIMS = [
  path.join('Libraries', 'Alert', 'RCTAlertManager.js'),
  path.join('Libraries', 'Components', 'AccessibilityInfo', 'legacySendAccessibilityEvent.js'),
  path.join('Libraries', 'Components', 'DrawerAndroid', 'DrawerLayoutAndroid.js'),
  path.join('Libraries', 'Components', 'ToastAndroid', 'ToastAndroid.js'),
  path.join('Libraries', 'Image', 'Image.js'),
  path.join('Libraries', 'NativeComponent', 'BaseViewConfig.js'),
  path.join('Libraries', 'Network', 'RCTNetworking.js'),
  path.join('Libraries', 'StyleSheet', 'PlatformColorValueTypes.js'),
  path.join('Libraries', 'Utilities', 'BackHandler.js'),
  // Platform.js belongs to this family too, but is answered by kind 3 below.
];

/**
 * Kind 3: modules this platform implements itself. Checked before the shim
 * list, so `Platform` gets ours rather than Android's.
 */
const PLATFORM_OVERRIDES = [
  [
    path.join('Libraries', 'Utilities', 'Platform.js'),
    path.join(OVERRIDE_DIR, 'Platform.linux.js'),
  ],
  [
    // Not a shim: React Native's TextInput.js branches on `Platform.OS` being
    // exactly 'android' or 'ios' and renders undefined on anything else. See
    // the header of the replacement for why it is a rewrite rather than a
    // third branch.
    path.join('Libraries', 'Components', 'TextInput', 'TextInput.js'),
    path.join(OVERRIDE_DIR, 'TextInput.linux.js'),
  ],
];

/**
 * Kind 2: modules React Native ships only as `.android.js` and `.ios.js`, with
 * no neutral file to resolve to. Keyed without an extension, because a request
 * that fails to resolve is only known by its extensionless path.
 */
const MISSING_MODULES = [
  [
    path.join('rndevtools', 'ReactDevToolsSettingsManager'),
    path.join(OVERRIDE_DIR, 'ReactDevToolsSettingsManager.linux.js'),
  ],
];

// Matched on the tail of a path rather than an absolute one: the React Native
// checkout can be a node_modules copy, a sibling clone or a workspace symlink.
function matchTail(filePath, table) {
  for (const [tail, replacement] of table) {
    if (filePath.endsWith(tail)) {
      return replacement;
    }
  }
  return null;
}

function replacementFor(filePath) {
  const own = matchTail(filePath, PLATFORM_OVERRIDES);
  if (own != null) {
    return own;
  }
  for (const shim of SELF_IMPORTING_SHIMS) {
    if (filePath.endsWith(shim)) {
      // The sibling beside the shim, wherever that directory happens to be.
      return filePath.replace(/\.js$/, '.android.js');
    }
  }
  return null;
}

/**
 * Adds `linux` to Metro's platforms and installs the overrides above.
 *
 * Composes with an existing `resolveRequest` rather than replacing it, so an
 * app that already has one keeps it.
 */
function withLinuxPlatform(config = {}) {
  const resolver = config.resolver ?? {};
  const existingResolveRequest = resolver.resolveRequest;
  const server = config.server ?? {};
  const existingRewrite = server.rewriteRequestUrl;

  const platforms = resolver.platforms ?? [];
  const withLinux = platforms.includes('linux') ? platforms : ['linux', ...platforms];

  // Metro will not read a file it is not watching, and this package hands it
  // files -- the overrides below. Inside this repo that is already true, since
  // `packages/` is on watchFolders; for anyone consuming the platform from a
  // linked checkout or another monorepo it is not, and bundling fails with
  // "Failed to get the SHA-1 for" the override rather than anything that names
  // the cause. Found by bundling a real app from another tree.
  const watchFolders = config.watchFolders ?? [];
  const withOverrides = watchFolders.some(folder => __dirname.startsWith(folder))
    ? watchFolders
    : [...watchFolders, __dirname];

  return {
    ...config,
    watchFolders: withOverrides,
    server: {
      ...server,
      // The dev server asks for the wrong platform, and cannot be told
      // otherwise: ReactCxxPlatform's DevServerHelper builds its bundle URL
      // from `constexpr DEFAULT_PLATFORM = "android"`, with no hook and no
      // setting. Left alone, an app would be `Platform.OS === 'android'` under
      // Fast Refresh and `'linux'` in a release build, which is a far worse
      // trap than either value on its own.
      //
      // So the request is corrected on arrival. Only for bundle requests, and
      // only when the platform is exactly `android`, so asking for an android
      // bundle from anything else still works.
      rewriteRequestUrl: url => {
        const rewritten = existingRewrite ? existingRewrite(url) : url;
        if (!/\.(bundle|map)\b/.test(rewritten)) {
          return rewritten;
        }
        return rewritten.replace(/([?&]platform=)android(&|$)/, '$1linux$2');
      },
    },
    resolver: {
      ...resolver,
      platforms: withLinux,
      resolveRequest: (context, moduleName, platform) => {
        // Resolve first, then decide. Rewriting the request instead would mean
        // reimplementing Metro's resolution to know what './Platform' meant
        // from any given file.
        let resolution;
        try {
          resolution = existingResolveRequest
            ? existingResolveRequest(context, moduleName, platform)
            : context.resolveRequest(context, moduleName, platform);
        } catch (error) {
          if (platform === 'linux' && moduleName.startsWith('.')) {
            const requested = path.resolve(
              path.dirname(context.originModulePath ?? ''),
              moduleName,
            );
            const forMissing = matchTail(requested, MISSING_MODULES);
            if (forMissing != null) {
              return {type: 'sourceFile', filePath: forMissing};
            }
          }
          throw error;
        }

        if (platform !== 'linux' || resolution?.type !== 'sourceFile') {
          return resolution;
        }

        const replacement = replacementFor(resolution.filePath);
        if (replacement == null || replacement === resolution.filePath) {
          return resolution;
        }

        // An override importing the thing it overrides would loop forever.
        if (path.resolve(context.originModulePath ?? '') === replacement) {
          return resolution;
        }

        return {type: 'sourceFile', filePath: replacement};
      },
    },
  };
}

module.exports = {
  withLinuxPlatform,
  SELF_IMPORTING_SHIMS,
  PLATFORM_OVERRIDES,
  MISSING_MODULES,
};
