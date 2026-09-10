/**
 * Building the host from an app.
 *
 * This is what `run-android` gets from Gradle and `run-ios` from Xcode. There
 * is no equivalent to hand it to, so the work is here: vendor React Native's
 * C++ dependencies, configure CMake against the app's own React Native, and
 * build.
 *
 * Two things about where output goes. It goes under the app, in `.rn-linux`,
 * not into `node_modules/react-native-linux`, because npm rewrites node_modules
 * on install and a Hermes build is not something to lose that way. And
 * `third_party` is shared between builds of the same app rather than per
 * configuration, because it depends on the React Native version and nothing
 * else; the version stamp inside it is what keeps that honest.
 *
 * The first build is slow in a way `run-android` is not. It compiles Hermes and
 * React Native's C++ core. Saying so before it starts is better than a silent
 * half hour.
 *
 * @format
 */

'use strict';

const {spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');

const {BINARY, isExecutable} = require('./host');

const NATIVE_DIR = path.resolve(__dirname, '..', 'native');

function run(command, args, options) {
  const result = spawnSync(command, args, {stdio: 'inherit', ...options});
  if (result.error) {
    if (result.error.code === 'ENOENT') {
      throw new Error(
        `${command} is not installed. Building the host needs cmake, ninja, a ` +
          'C++20 compiler, and the GTK4 and Pango development packages.',
      );
    }
    throw result.error;
  }
  if (result.status !== 0) {
    throw new Error(`${command} ${args[0] ?? ''} failed`);
  }
}

/**
 * The React Native monorepo root, which is what bootstrap wants.
 *
 * `reactNativePath` is the package directory, two levels below the root in a
 * checkout. An installed react-native has no monorepo above it, and bootstrap
 * needs one because it runs React Native's own codegen script from there.
 */
function monorepoRoot(reactNativePath) {
  // Through the symlink first. A workspace, a pnpm store and `npm link` all
  // put a link in node_modules, and walking up from the link lands in the app
  // rather than in the checkout it points at.
  let real = reactNativePath;
  try {
    real = fs.realpathSync(reactNativePath);
  } catch {
    // Keep the original and let the check below decide.
  }

  const candidate = path.resolve(real, '..', '..');
  if (fs.existsSync(path.join(candidate, 'packages', 'react-native'))) {
    return candidate;
  }
  return null;
}

function buildHost(context, options) {
  const projectRoot = context.root;
  const workDir = path.join(projectRoot, '.rn-linux');
  const buildDir = path.join(workDir, 'build');
  const thirdParty = path.join(workDir, 'third_party');

  const reactNativeRoot = monorepoRoot(context.reactNativePath);
  if (reactNativeRoot == null) {
    throw new Error(
      'building the host needs a React Native source checkout, and this project ' +
        `resolves react-native to ${context.reactNativePath}, which is an ` +
        'installed package rather than a checkout.\n\n' +
        'That is a real limitation, not a misconfiguration: the host is compiled ' +
        'against React Native\'s C++ sources, and the npm package does not ship ' +
        'them. Point at a checkout of the same version with --react-native-path, ' +
        'or build the host yourself and pass --host-binary.',
    );
  }

  fs.mkdirSync(workDir, {recursive: true});

  console.log('==> vendoring React Native\'s C++ dependencies');
  console.log('    The first run compiles Hermes and takes a while.');
  run(path.join(NATIVE_DIR, 'bootstrap.sh'), [reactNativeRoot], {
    cwd: workDir,
    env: {...process.env, RN_LINUX_THIRD_PARTY: thirdParty},
  });

  console.log('==> configuring');
  run('cmake', [
    '-S',
    NATIVE_DIR,
    '-B',
    buildDir,
    '-G',
    'Ninja',
    `-DRN_DIR=${path.join(reactNativeRoot, 'packages', 'react-native')}`,
    `-DRN_LINUX_THIRD_PARTY=${thirdParty}`,
  ]);

  console.log('==> building');
  const jobs = options.jobs ? ['-j', String(options.jobs)] : [];
  run('cmake', ['--build', buildDir, ...jobs]);

  const binary = path.join(buildDir, BINARY);
  if (!isExecutable(binary)) {
    throw new Error(`the build produced no ${BINARY} at ${binary}`);
  }
  return binary;
}

module.exports = {buildHost, NATIVE_DIR};
