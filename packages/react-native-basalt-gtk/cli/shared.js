/**
 * Reaching react-native-basalt, the shared half, from this package.
 *
 * Two layouts matter and only one of them resolves by name.
 *
 * Installed into an app, react-native-basalt is a sibling in node_modules and
 * `require('react-native-basalt/...')` finds it. In a checkout of this
 * repository the two packages are sibling directories with nothing installed
 * between them -- deliberately, since this repo has no node_modules of its own
 * -- and the same require fails. So: by name first, because that is the layout
 * an app is in, and beside us second, because that is the layout it is
 * developed in.
 *
 * @format
 */

'use strict';

const path = require('path');

const SIBLING = path.resolve(__dirname, '..', '..', 'react-native-basalt');

/**
 * Where react-native-basalt is on disk. Same two layouts, same order.
 */
function sharedPackageDir() {
  try {
    return path.dirname(require.resolve('react-native-basalt/package.json'));
  } catch (error) {
    if (error.code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    return SIBLING;
  }
}

function shared(subpath) {
  const name = `react-native-basalt/${subpath}`;
  try {
    return require(name);
  } catch (error) {
    if (error.code !== 'MODULE_NOT_FOUND') {
      // The module was found and threw on its way up. Passing it through keeps
      // a real error in that file from being reported as a missing package.
      throw error;
    }
    return require(path.join(SIBLING, subpath.replace(/^cli\//, 'cli/')));
  }
}

module.exports = {shared, sharedPackageDir};
