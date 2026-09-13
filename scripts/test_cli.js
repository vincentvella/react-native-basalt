/**
 * Tests for the desktop CLI: `run-linux`, `run-macos` and `run-windows`.
 *
 * The three are one command with three names, and everything they share lives
 * in react-native-basalt's cli/desktop.js. That sharing is the thing worth
 * testing: a change made for one desktop lands on all three, and the failure
 * mode is an app that bundles for the wrong platform or looks for the wrong
 * binary -- neither of which any C++ test can see.
 *
 * Pure functions only. Nothing here spawns cmake, starts Metro or launches a
 * host; what is checked is the decisions those steps are handed.
 *
 * Run with:  node --test scripts/test_cli.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {test} = require('node:test');

const REPO = path.resolve(__dirname, '..');
const desktop = require(path.join(REPO, 'packages/react-native-basalt/cli/desktop.js'));

// The three real command modules, loaded the way React Native's CLI loads them:
// through each platform package's react-native.config.js. That this resolves at
// all is half the test -- the shared half is reached by a two-step require that
// only works in the two layouts described in cli/shared.js.
const CONFIGS = {
  linux: path.join(REPO, 'packages/react-native-basalt-gtk/react-native.config.js'),
  macos: path.join(REPO, 'packages/react-native-basalt-appkit/react-native.config.js'),
  windows: path.join(REPO, 'packages/react-native-basalt-win32/react-native.config.js'),
};

const EXPECTED = {
  linux: {command: 'run-linux', binary: 'basalt_gtk'},
  macos: {command: 'run-macos', binary: 'basalt_appkit'},
  windows: {command: 'run-windows', binary: 'basalt_win32.exe'},
};

function scratch() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-cli-'));
}

// A target of the shape a platform package passes in, for the pieces that take
// one directly.
function target(platform) {
  return {
    platform,
    label: platform,
    command: EXPECTED[platform].command,
    binary: EXPECTED[platform].binary,
    nativeDir: path.join(REPO, 'packages', 'somewhere', 'native'),
    coreDir: path.join(REPO, 'packages/react-native-basalt/native'),
    toolchain: 'a compiler',
  };
}

test('every platform package contributes exactly one command', () => {
  for (const [platform, configPath] of Object.entries(CONFIGS)) {
    const config = require(configPath);
    assert.equal(config.commands.length, 1, `${platform} should export one command`);

    const command = config.commands[0];
    assert.equal(command.name, EXPECTED[platform].command);
    assert.equal(typeof command.func, 'function');

    // The binary name reaches the user, in --host-binary's help and in the
    // message when no host is found. Getting it wrong sends them looking for a
    // file that was never going to exist.
    const hostOption = command.options.find(option =>
      option.name.startsWith('--host-binary'),
    );
    assert.ok(
      hostOption.description.includes(EXPECTED[platform].binary),
      `${platform} should name ${EXPECTED[platform].binary}`,
    );
  }
});

test('the three commands offer the same options', () => {
  const names = platform =>
    require(CONFIGS[platform]).commands[0].options.map(option => option.name).sort();

  // Not a tautology now that they are one function, but it is the property the
  // sharing exists to guarantee -- and the one a future per-platform special
  // case would quietly break.
  assert.deepEqual(names('linux'), names('macos'));
  assert.deepEqual(names('linux'), names('windows'));
  assert.ok(names('linux').includes('--mode <string>'));
  assert.ok(names('linux').includes('--build'));
});

test('a directory is not an executable, and a missing file is not either', () => {
  const directory = scratch();
  assert.equal(desktop.isExecutable(directory), false);
  assert.equal(desktop.isExecutable(path.join(directory, 'nothing')), false);

  const file = path.join(directory, 'basalt_win32.exe');
  fs.writeFileSync(file, '');
  // Executable on Windows, where the question is only whether it is a file;
  // not on Unix, where it was written without the bit.
  assert.equal(desktop.isExecutable(file), process.platform === 'win32');

  fs.rmSync(directory, {recursive: true, force: true});
});

test('the host is looked for in the explicit places first', () => {
  const project = scratch();
  const looked = desktop.candidates(project, {hostBinary: 'somewhere/host'}, target('linux'));

  assert.equal(looked[0], path.join(project, 'somewhere', 'host'));
  // .basalt/build is where --build puts one, and it must beat the checkout's
  // build tree or a developer's stale binary would win over their fresh one.
  assert.ok(looked.some(entry => entry.includes(path.join('.basalt', 'build'))));
  assert.ok(looked.every(entry => entry.endsWith('basalt_gtk') || entry.endsWith('host')));

  fs.rmSync(project, {recursive: true, force: true});
});

test('a missing host explains how to make one rather than printing a path', () => {
  const project = scratch();
  // The package's native directory is a scratch one too. The last place the
  // host is looked for is the development checkout's build tree, found from
  // nativeDir -- and with nativeDir inside this repository, that is this
  // repository's build/, which on CI holds a real basalt_win32.exe by the time
  // this runs. The test passed for as long as it ran before the build, and
  // failed the first time it ran after one.
  const windows = {...target('windows'), nativeDir: path.join(project, 'pkg', 'native')};
  const previous = process.env.BASALT_HOST;
  delete process.env.BASALT_HOST;
  let thrown = null;
  try {
    desktop.resolveHost(project, {}, windows);
  } catch (error) {
    thrown = error;
  } finally {
    if (previous !== undefined) {
      process.env.BASALT_HOST = previous;
    }
  }

  assert.ok(thrown instanceof desktop.MissingHost);
  assert.ok(thrown.message.includes('react-native run-windows --build'));
  assert.ok(thrown.message.includes('basalt_win32.exe'));
  // The toolchain sentence is the platform's own, and it is the only part of
  // that message a reader can act on if they have nothing installed.
  assert.ok(thrown.message.includes('a compiler'));

  fs.rmSync(project, {recursive: true, force: true});
});

test('BASALT_HOST wins over everything but --host-binary', () => {
  const project = scratch();
  const host = path.join(project, 'from-the-environment');
  fs.writeFileSync(host, '');
  fs.chmodSync(host, 0o755);

  const previous = process.env.BASALT_HOST;
  process.env.BASALT_HOST = host;
  try {
    assert.equal(desktop.resolveHost(project, {}, target('linux')), path.normalize(host));
  } finally {
    if (previous === undefined) {
      delete process.env.BASALT_HOST;
    } else {
      process.env.BASALT_HOST = previous;
    }
    fs.rmSync(project, {recursive: true, force: true});
  }
});

test('the module name comes from --module, then app.json', () => {
  const project = scratch();

  assert.equal(desktop.resolveModuleName(project, {module: 'Explicit'}), 'Explicit');

  // No app.json and no --module: the error has to say what a module name *is*,
  // because "no module name" is meaningless to someone who has never written
  // an AppRegistry call by hand.
  assert.throws(
    () => desktop.resolveModuleName(project, {}),
    /AppRegistry\.registerComponent/,
  );

  fs.writeFileSync(path.join(project, 'app.json'), JSON.stringify({name: 'FromAppJson'}));
  assert.equal(desktop.resolveModuleName(project, {}), 'FromAppJson');
  // Still second to --module: an Expo app's app.json says one thing and
  // registerRootComponent registers "main".
  assert.equal(desktop.resolveModuleName(project, {module: 'main'}), 'main');

  fs.writeFileSync(path.join(project, 'app.json'), 'not json');
  assert.throws(() => desktop.resolveModuleName(project, {}), /could not read/);

  fs.rmSync(project, {recursive: true, force: true});
});

test('a monorepo root is only found above a real checkout', () => {
  const root = scratch();

  // What an installed react-native looks like: no packages/react-native above.
  const installed = path.join(root, 'app', 'node_modules', 'react-native');
  fs.mkdirSync(installed, {recursive: true});
  assert.equal(desktop.monorepoRoot(installed), null);

  // And what a checkout looks like.
  const checkout = path.join(root, 'checkout', 'packages', 'react-native');
  fs.mkdirSync(checkout, {recursive: true});
  assert.equal(desktop.monorepoRoot(checkout), path.join(root, 'checkout'));

  fs.rmSync(root, {recursive: true, force: true});
});
