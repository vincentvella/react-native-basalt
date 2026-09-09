// Metro configuration for a project with no node_modules of its own.
//
// This directory is not an npm package in the usual sense: nothing is
// installed into it. `react`, `react-native`, Metro and the Babel preset all
// resolve out of the React Native checkout that scripts/bootstrap.sh prepared,
// which keeps the demo in step with the exact source tree the C++ side is built
// against. RN_DIR points at it, the same variable CMake takes.
//
// Three settings do the work:
//   watchFolders        lets Metro read files outside this directory at all.
//   nodeModulesPaths    is where `require('react-native')` is looked up.
//   extraNodeModules    pins react to one copy, so hooks do not blow up.

const path = require('path');
const fs = require('fs');
const {createRequire} = require('module');

const rnDir = path.resolve(
  process.env.RN_DIR ?? path.resolve(__dirname, '..', '..', 'react-native'),
);

if (!fs.existsSync(path.join(rnDir, 'node_modules', 'react-native'))) {
  throw new Error(
    `No React Native checkout with installed dependencies at ${rnDir}.\n` +
      'Point RN_DIR at one, and run scripts/bootstrap.sh if its node_modules is empty.',
  );
}

// Resolve as package specifiers rather than by path. These are workspace
// packages whose entry point is declared only in "exports", and Node ignores
// "exports" when you require an absolute directory path.
const rnRequire = createRequire(path.join(rnDir, 'package.json'));
const {getDefaultConfig, mergeConfig} = rnRequire('@react-native/metro-config');

// The `linux` platform itself: adds it to Metro's platform list and puts this
// project's replacements in front of the React Native modules that have no
// .linux variant. Without it, bundling with --platform linux produces a bundle
// that builds and then dies on Platform.constants being undefined.
const {withLinuxPlatform} = require('../packages/react-native-linux/metro-config');

module.exports = withLinuxPlatform(mergeConfig(getDefaultConfig(__dirname), {
  projectRoot: __dirname,
  watchFolders: [rnDir, path.resolve(__dirname, '..', 'packages')],
  resolver: {
    nodeModulesPaths: [path.join(rnDir, 'node_modules')],
    // Without this, two copies of React can end up in the graph -- one
    // resolved from here and one from a transitive dependency inside the
    // checkout -- and hooks fail at runtime with the usual invalid-hook-call.
    extraNodeModules: {
      react: path.join(rnDir, 'node_modules', 'react'),
      'react-native': path.join(rnDir, 'node_modules', 'react-native'),
      'react-native-linux': path.resolve(__dirname, '..', 'packages', 'react-native-linux'),
    },
  },
}));
