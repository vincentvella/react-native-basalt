/**
 * What React Native's CLI reads out of this package.
 *
 * An entry per platform, without which the CLI does not believe these are
 * platforms and `react-native bundle --platform macos` is rejected before Metro
 * sees it.
 *
 * Commands are not here. `run-linux` needs a GTK host to run, so it belongs to
 * react-native-basalt-gtk -- the CLI reads a react-native.config.js out of every
 * dependency, so an app that has only the AppKit package never sees a command it
 * could not have used.
 *
 * `projectConfig` returning null for a project with no native directory is how
 * the CLI is told "this project does not target that platform", and is what
 * keeps the command out of the way in an app that only builds for iOS. Today
 * every project targets all three, because there is no per-project native code
 * to look for yet; when there is, this is where the check goes.
 *
 * @format
 */

'use strict';

const DESKTOP_PLATFORMS = require('./metro-config').DESKTOP_PLATFORMS;

// The same entry for each. They differ in their view layer and in nothing the
// CLI can see, which is the whole argument of this project restated as four
// lines of configuration.
//
// Deliberately no `npmPackageName`, which is the one field that looks obviously
// right and is wrong here.
//
// Setting it tells the CLI this is an *out-of-tree platform* in the
// react-native-macos and react-native-windows sense: a fork. It then installs
// `reactNativePlatformResolver`, which rewrites every `react-native/...` import
// to `react-native-basalt/...` when bundling, and requires this package to
// export `./setup-env`. Both are correct for a fork that vendors React Native's
// JavaScript. This package vendors none of it -- it redirects nine of React
// Native's own shims to their existing `.android.js` siblings and overrides two
// files -- so the rewrite sends every import into a package that does not
// contain it.
//
// Without the field these are still platforms the CLI accepts, and resolution
// stays with `withDesktopPlatforms` in metro-config.js, which is where the
// actual policy lives. See plan/12-run-linux.md.
const platforms = {};
for (const name of DESKTOP_PLATFORMS) {
  platforms[name] = {
    projectConfig: () => ({}),
    // No autolinking: nothing here has native code to link yet, and claiming
    // otherwise would make the CLI generate references to files that do not
    // exist. See plan/12-run-linux.md.
    dependencyConfig: () => null,
  };
}

module.exports = {platforms};
