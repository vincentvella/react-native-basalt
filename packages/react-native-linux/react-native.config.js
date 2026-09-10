/**
 * What React Native's CLI reads out of this package.
 *
 * Two things. The `run-linux` command, which is the point. And a `linux` entry
 * under platforms, without which the CLI does not believe `linux` is a platform
 * and `react-native bundle --platform linux` is rejected before Metro sees it.
 *
 * `projectConfig` returning null for a project with no linux directory is how
 * the CLI is told "this project does not target linux", and is what keeps the
 * command out of the way in an app that only builds for iOS. Today every
 * project targets it, because there is no per-project native code to look for
 * yet; when there is, this is where the check goes.
 *
 * @format
 */

'use strict';

module.exports = {
  commands: [require('./cli/runLinux')],
  platforms: {
    linux: {
      // Deliberately no `npmPackageName`, which is the one field that looks
      // obviously right and is wrong here.
      //
      // Setting it tells the CLI this is an *out-of-tree platform* in the
      // react-native-macos and react-native-windows sense: a fork. It then
      // installs `reactNativePlatformResolver`, which rewrites every
      // `react-native/...` import to `react-native-linux/...` when bundling for
      // linux, and requires this package to export `./setup-env`. Both are
      // correct for a fork that vendors React Native's JavaScript. This package
      // vendors none of it -- it redirects nine of React Native's own shims to
      // their existing `.android.js` siblings and overrides two files -- so the
      // rewrite sends every import into a package that does not contain it.
      //
      // Without the field, `linux` is still a platform the CLI accepts, and
      // resolution stays with `withLinuxPlatform` in metro-config.js, which is
      // where this platform's actual policy lives. See plan/12-run-linux.md.
      projectConfig: () => ({}),
      // No autolinking: nothing on this platform has native code to link yet,
      // and claiming otherwise would make the CLI generate references to files
      // that do not exist. See plan/12-run-linux.md.
      dependencyConfig: () => null,
    },
  },
};
