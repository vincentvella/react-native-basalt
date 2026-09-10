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

const BINARY = 'rn_linux_host';

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
  if (process.env.RN_LINUX_HOST) {
    found.push(path.resolve(process.env.RN_LINUX_HOST));
  }
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
      'react-native-linux does not build the host yet. Build it from a checkout:\n\n' +
      '  scripts/bootstrap.sh /path/to/react-native\n' +
      '  cmake -B build -G Ninja -DRN_DIR=/path/to/react-native/packages/react-native\n' +
      '  cmake --build build\n\n' +
      `then point at it with --host-binary <path> or RN_LINUX_HOST.`,
  );
}

module.exports = {BINARY, MissingHost, resolveHost, isExecutable};
