/**
 * Metro configuration for this project's desktop platforms.
 *
 * An out-of-tree React Native platform has to do two things to Metro, and the
 * second is the one that is not obvious.
 *
 * First, the platform has to be one Metro knows about, so that
 * `Button.macos.js` wins over `Button.js` the way `Button.android.js` would.
 *
 * Second, some of React Native's own modules cannot work on a platform it has
 * never heard of. There are three kinds, and each needs different handling:
 *
 *   1. **Self-importing shims.** React Native ships a family of files whose
 *      entire body is `import X from './X'; export default X;`, each marked
 *      with a note about "backwards compatibility of subpath (deep) imports".
 *      They exist so `react-native/Libraries/Image/Image` keeps working, and
 *      they rely on a platform-specific sibling winning the resolution. Bundle
 *      for one of these platforms and each resolves to *itself*, exports
 *      undefined, and kills whatever touches it -- as `Platform.constants`
 *      being undefined, or a view config being undefined, or a component being
 *      undefined.
 *
 *   2. **Modules with no neutral fallback at all**, which do not resolve, so
 *      there is no resolution to rewrite -- only a failure to intercept.
 *
 *   3. **Modules this project genuinely implements differently**, which is
 *      `Platform` and `TextInput`.
 *
 * React Native for Windows solves all of this with a whole override system and
 * a fork of every file it replaces. This is the same idea at the smallest size
 * that works.
 *
 * None of the three kinds is Linux-specific, which is the point of this file's
 * shape: the answers are identical for linux, macos and windows, and only
 * `Platform` differs, by one string. Anything that has to be written per
 * platform is a `.linux.js` / `.macos.js` / `.windows.js` file in
 * `src/overrides`, and the tables below name it.
 *
 * Usage, in an app's metro.config.js:
 *
 *     const {withDesktopPlatforms} = require('react-native-basalt/metro-config');
 *     module.exports = withDesktopPlatforms(config);
 *
 * @format
 */

'use strict';

const path = require('path');

const OVERRIDE_DIR = path.join(__dirname, 'src', 'overrides');

/**
 * The platforms this package can bundle for.
 *
 * `macos` and `windows` deliberately match the names react-native-macos and
 * react-native-windows use. A library that ships `Button.macos.js` for those
 * forks resolves correctly here without knowing this project exists, and a
 * library that does not is no worse off. Picking different names would have
 * bought nothing and cost that.
 */
const DESKTOP_PLATFORMS = ['linux', 'macos', 'windows'];

/**
 * Kind 1: React Native's self-importing deep-import shims.
 *
 * Each is answered with its own `.android.js` sibling rather than a file of
 * ours. That is deliberate: these platforms report `PlatformConstantsAndroid`
 * from C++, share ReactCommon's prop parsing, and drive the same components
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
 * Kind 3: modules this project implements itself. Checked before the shim list,
 * so `Platform` gets ours rather than Android's.
 *
 * A replacement is either a path, when every platform shares it, or a function
 * of the platform when they do not. Only `Platform` needs the second form, and
 * the one-line files it points at exist so that the difference between the
 * platforms stays exactly one string. See src/overrides/createPlatform.js.
 */
const PLATFORM_OVERRIDES = [
  [
    path.join('Libraries', 'Utilities', 'Platform.js'),
    platform => path.join(OVERRIDE_DIR, `Platform.${platform}.js`),
  ],
  [
    // Not a shim: React Native's TextInput.js branches on `Platform.OS` being
    // exactly 'android' or 'ios' and renders undefined on anything else. See
    // the header of the replacement for why it is a rewrite rather than a
    // third branch.
    path.join('Libraries', 'Components', 'TextInput', 'TextInput.js'),
    path.join(OVERRIDE_DIR, 'TextInput.js'),
  ],
  [
    // Also not a shim. `fetch` is broken on this platform without it: every
    // request asks for a blob response, which ReactCxxPlatform's
    // NetworkingModule cannot produce, so the response getter throws before
    // whatwg-fetch builds a Response. The replacement is React Native's own
    // file with the blob response type routed through base64; see its header.
    path.join('Libraries', 'Core', 'setUpXHR.js'),
    path.join(OVERRIDE_DIR, 'setUpXHR.js'),
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
    path.join(OVERRIDE_DIR, 'ReactDevToolsSettingsManager.js'),
  ],
];

/**
 * Kind 4: a *library* that ships only `.ios` and `.android` files.
 *
 * `react-native-screens` has `TabsScreen.ios.tsx`, `TabsScreen.android.tsx` and
 * `TabsScreen.web.tsx`, and an `index.ts` that says `from './TabsScreen'`.
 * Resolution fails, and the whole bundle fails with it -- one component nothing
 * in the app renders takes down a build that would otherwise have worked. Every
 * library that has never heard of this platform is a candidate, which is all of
 * them, so this cannot be a list of names.
 *
 * So a resolution that fails for a desktop platform is retried as another
 * platform, in this order, and the first that resolves wins. The build gets a
 * real implementation of the module rather than failing; if that implementation
 * needs a native module this platform does not have, it fails at runtime like
 * any other unsupported library, which is a much better place to fail.
 *
 * Android first, for the same reason `SELF_IMPORTING_SHIMS` are answered with
 * their `.android.js` sibling: these platforms report `PlatformConstantsAndroid`
 * and share ReactCommon's prop parsing. One order for all three desktops rather
 * than a per-platform guess, because two desktops resolving *different*
 * implementations of the same library is the one outcome worse than either.
 *
 * Set `platformFallbacks: []` to turn this off and get the resolution error.
 */
const PLATFORM_FALLBACKS = ['android', 'ios'];

/**
 * How a host tells the dev server which desktop it is.
 *
 * ReactCxxPlatform's DevServerHelper builds its bundle URL from
 * `constexpr DEFAULT_PLATFORM = "android"`, with no hook and no setting, so
 * every desktop host asks Metro for an android bundle. Left alone, an app would
 * be `Platform.OS === 'android'` under Fast Refresh and its real value in a
 * release build, which is a far worse trap than either on its own.
 *
 * The request is therefore corrected on arrival, and the only thing in it that
 * a host controls is `app=`, which comes from `ReactInstanceConfig::appId` and
 * which Metro itself ignores. So the hosts set that to
 * `basalt-<platform>` and this reads it back.
 *
 * It is a workaround and it should not have to exist. The fix is a `platform`
 * field on ReactInstanceConfig, upstream; see plan/21-js-platform-layer.md.
 */
const APP_ID_PREFIX = 'basalt-';

function appIdFor(platform) {
  return APP_ID_PREFIX + platform;
}

// Whether `parent` contains `child`, or is it.
//
// Not `child.startsWith(parent)`. A React Native checkout at
// `/src/react-native` is a string prefix of this package at
// `/src/react-native-basalt/packages/react-native-basalt`, so the naive test
// reports that the package is already being watched when it is not, and Metro
// then refuses to read the very files this plugin hands it. Which is exactly
// the layout this repository is developed in.
function contains(parent, child) {
  const relative = path.relative(parent, child);
  return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative));
}

// Matched on the tail of a path rather than an absolute one: the React Native
// checkout can be a node_modules copy, a sibling clone or a workspace symlink.
function matchTail(filePath, table, platform) {
  for (const [tail, replacement] of table) {
    if (filePath.endsWith(tail)) {
      return typeof replacement === 'function' ? replacement(platform) : replacement;
    }
  }
  return null;
}

function replacementFor(filePath, platform) {
  const own = matchTail(filePath, PLATFORM_OVERRIDES, platform);
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
 * Rewrites a dev-server bundle request that says `android` to the desktop
 * platform that actually asked for it. See APP_ID_PREFIX above.
 *
 * `fallback` is used when the request carries no `app=` this plugin recognises,
 * which is what an older host or a hand-typed URL looks like.
 */
function correctBundlePlatform(url, platforms, fallback) {
  if (!/\.(bundle|map)\b/.test(url)) {
    return url;
  }
  if (!/([?&]platform=)android(&|$)/.test(url)) {
    return url;
  }

  const app = /[?&]app=([^&]*)/.exec(url);
  let target = fallback;
  if (app != null && app[1].startsWith(APP_ID_PREFIX)) {
    const named = app[1].slice(APP_ID_PREFIX.length);
    if (platforms.includes(named)) {
      target = named;
    }
  }
  if (target == null) {
    return url;
  }
  return url.replace(/([?&]platform=)android(&|$)/, `$1${target}$2`);
}

/**
 * Adds this project's desktop platforms to Metro and installs the overrides.
 *
 * Composes with an existing `resolveRequest` and `rewriteRequestUrl` rather
 * than replacing them, so an app that already has either keeps it.
 *
 * Options:
 *
 *   platforms          which of linux/macos/windows to enable. All of them by
 *                      default: bundling is per-platform anyway, so there is
 *                      nothing to be gained by making an app declare a subset.
 *   devServerPlatform  what to assume a dev-server request is for when it does
 *                      not say. Defaults to the first enabled platform. A host
 *                      built from this repo always says, so this only matters
 *                      for a URL typed by hand.
 *   platformFallbacks  which platforms to retry a failed resolution as, in
 *                      order. See PLATFORM_FALLBACKS. `[]` disables it.
 */
function withDesktopPlatforms(config = {}, options = {}) {
  const enabled = options.platforms ?? DESKTOP_PLATFORMS;
  const devServerPlatform = options.devServerPlatform ?? enabled[0];
  const fallbacks = options.platformFallbacks ?? PLATFORM_FALLBACKS;
  // One line per module, not per import of it: a library resolved through the
  // fallback is usually imported from a dozen places.
  const reported = new Set();

  const resolver = config.resolver ?? {};
  const existingResolveRequest = resolver.resolveRequest;
  const server = config.server ?? {};
  const existingRewrite = server.rewriteRequestUrl;

  const platforms = resolver.platforms ?? [];
  const withDesktop = [
    ...enabled.filter(name => !platforms.includes(name)),
    ...platforms,
  ];

  // Metro will not read a file it is not watching, and this package hands it
  // files -- the overrides above. Inside this repo that is already true, since
  // `packages/` is on watchFolders; for anyone consuming the platform from a
  // linked checkout or another monorepo it is not, and bundling fails with
  // "Failed to get the SHA-1 for" the override rather than anything that names
  // the cause. Found by bundling a real app from another tree.
  const watchFolders = config.watchFolders ?? [];
  const withOverrides = watchFolders.some(folder => contains(folder, __dirname))
    ? watchFolders
    : [...watchFolders, __dirname];

  // Metro resolves this package's files by their real path and then looks for
  // their dependencies by walking up from there. Installed normally that lands
  // in the app's node_modules and everything is found. Linked from a checkout,
  // or in a monorepo, it lands in this repository instead, where the app's
  // dependencies are not -- and the failure names @babel/runtime, a helper this
  // package's own compiled output needs, rather than naming the link.
  //
  // Naming the project's node_modules explicitly covers both.
  const projectRoot = config.projectRoot ?? process.cwd();
  const projectModules = path.join(projectRoot, 'node_modules');
  const nodeModulesPaths = resolver.nodeModulesPaths ?? [];
  const withProject = nodeModulesPaths.includes(projectModules)
    ? nodeModulesPaths
    : [...nodeModulesPaths, projectModules];

  return {
    ...config,
    watchFolders: withOverrides,
    server: {
      ...server,
      rewriteRequestUrl: url =>
        correctBundlePlatform(
          existingRewrite ? existingRewrite(url) : url,
          enabled,
          devServerPlatform,
        ),
    },
    resolver: {
      ...resolver,
      platforms: withDesktop,
      nodeModulesPaths: withProject,
      resolveRequest: (context, moduleName, platform) => {
        const ours = enabled.includes(platform);

        // Resolve first, then decide. Rewriting the request instead would mean
        // reimplementing Metro's resolution to know what './Platform' meant
        // from any given file.
        const resolveAs = target =>
          existingResolveRequest
            ? existingResolveRequest(context, moduleName, target)
            : context.resolveRequest(context, moduleName, target);

        let resolution;
        try {
          resolution = resolveAs(platform);
        } catch (error) {
          if (!ours) {
            throw error;
          }

          if (moduleName.startsWith('.')) {
            const requested = path.resolve(
              path.dirname(context.originModulePath ?? ''),
              moduleName,
            );
            const forMissing = matchTail(requested, MISSING_MODULES, platform);
            if (forMissing != null) {
              return {type: 'sourceFile', filePath: forMissing};
            }
          }

          // Kind 4. The original error is what gets thrown if every fallback
          // fails too: it names the platform the app actually asked for.
          resolution = null;
          for (const fallback of fallbacks) {
            try {
              resolution = resolveAs(fallback);
            } catch {
              continue;
            }
            const key = `${moduleName}\u0000${context.originModulePath ?? ''}`;
            if (!reported.has(key)) {
              reported.add(key);
              console.warn(
                `basalt: '${moduleName}' has no ${platform} implementation; ` +
                  `using its ${fallback} one (from ${context.originModulePath ?? '?'})`,
              );
            }
            break;
          }
          if (resolution == null) {
            throw error;
          }
        }

        if (!ours || resolution?.type !== 'sourceFile') {
          return resolution;
        }

        const replacement = replacementFor(resolution.filePath, platform);
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

/**
 * The original name, kept working. Enables only `linux`, which is what it
 * always did, so an app that has this in its metro.config.js keeps the exact
 * behaviour it had.
 */
function withLinuxPlatform(config = {}) {
  return withDesktopPlatforms(config, {platforms: ['linux']});
}

module.exports = {
  withDesktopPlatforms,
  withLinuxPlatform,
  appIdFor,
  APP_ID_PREFIX,
  DESKTOP_PLATFORMS,
  SELF_IMPORTING_SHIMS,
  PLATFORM_OVERRIDES,
  MISSING_MODULES,
};
