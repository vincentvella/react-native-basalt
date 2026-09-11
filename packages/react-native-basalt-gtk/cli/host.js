/**
 * Finding the host binary.
 *
 * `run-android` hands the build to Gradle and `run-ios` to Xcode. This platform
 * has no equivalent yet: the host is a C++ binary built by CMake out of this
 * repository, against one specific React Native. So the command does not build
 * it, it finds it, and when it cannot it says exactly how to make one rather
 * than failing with a path.
 *
 * Building it on demand is the obvious next step and a large one, because it
 * means the npm package carrying the C++ sources and a bootstrap that fetches
 * folly and Hermes. See plan/12-run-linux.md.
 *
 * @format
 */

'use strict';

const fs = require('fs');
const path = require('path');

const BINARY = 'basalt_gtk';

/**
 * Where to look, in order of how explicit the answer is.
 *
 * The last entry is the development checkout: when this package is being used
 * from inside its own repository, the build tree is four directories up. That
 * is what makes the demo app in examples/ work without configuration.
 */
function candidates(projectRoot, options) {
  const found = [];

  if (options.hostBinary) {
    found.push(path.resolve(projectRoot, options.hostBinary));
  }
  if (process.env.BASALT_HOST) {
    found.push(path.resolve(process.env.BASALT_HOST));
  }
  // Where `run-linux --build` puts one.
  found.push(path.join(projectRoot, '.basalt', 'build', BINARY));
  found.push(path.join(projectRoot, 'linux', 'build', BINARY));
  found.push(path.join(projectRoot, 'build', BINARY));
  found.push(path.resolve(__dirname, '..', '..', '..', 'build', BINARY));

  return found;
}

function isExecutable(file) {
  try {
    fs.accessSync(file, fs.constants.X_OK);
    return fs.statSync(file).isFile();
  } catch {
    return false;
  }
}

class MissingHost extends Error {}

function resolveHost(projectRoot, options) {
  const looked = candidates(projectRoot, options);
  const found = looked.find(isExecutable);
  if (found) {
    return found;
  }

  throw new MissingHost(
    `could not find ${BINARY}.\n\n` +
      'Looked in:\n' +
      looked.map(entry => `  ${entry}`).join('\n') +
      '\n\n' +
      'Build one with:\n\n' +
      '  react-native run-linux --build\n\n' +
      'That needs a React Native source checkout, cmake, ninja, a C++20 ' +
      'compiler,\nand the GTK4 and Pango development packages. Or build it ' +
      'yourself and point\nat it with --host-binary <path> or BASALT_HOST.',
  );
}

module.exports = {BINARY, MissingHost, resolveHost, isExecutable};
