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
  // Resolved, because the code under test resolves too. `monorepoRoot` calls
  // realpathSync deliberately -- a workspace, a pnpm store and `npm link` all
  // put a symlink in node_modules, and walking up from the link lands in the
  // app rather than in the checkout it points at -- so it returns a real path
  // and a test comparing against an unresolved one fails.
  //
  // Only on macOS, where os.tmpdir() is /var/folders/... and /var is a symlink
  // to /private/var. Linux's /tmp is not a symlink and Windows has no such
  // thing, so this passed in CI and failed on the machine the macOS work was
  // being done on.
  return fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-cli-')));
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

test('an installed react-native is built from, not refused', () => {
  const project = scratch();

  // What every app has: the package, with ReactCommon in it and no monorepo
  // above it.
  const installed = path.join(project, 'node_modules', 'react-native');
  fs.mkdirSync(path.join(installed, 'ReactCommon'), {recursive: true});
  const fromPackage = desktop.reactNativeSources(installed);
  assert.equal(fromPackage.layout, 'installed');
  assert.equal(fromPackage.bootstrapArg, fs.realpathSync(installed));
  assert.equal(fromPackage.rnDir, fs.realpathSync(installed));

  // A checkout still hands bootstrap the root and CMake the package under it.
  const checkout = path.join(project, 'react-native');
  fs.mkdirSync(path.join(checkout, 'packages', 'react-native', 'ReactCommon'), {recursive: true});
  const fromCheckout = desktop.reactNativeSources(
    path.join(checkout, 'packages', 'react-native'),
  );
  assert.equal(fromCheckout.layout, 'checkout');
  assert.equal(fromCheckout.bootstrapArg, fs.realpathSync(checkout));
  assert.equal(
    fromCheckout.rnDir,
    path.join(fs.realpathSync(checkout), 'packages', 'react-native'),
  );

  // Neither: say so, rather than handing bootstrap a directory it will refuse.
  const empty = path.join(project, 'empty');
  fs.mkdirSync(empty);
  assert.throws(() => desktop.reactNativeSources(empty), /no ReactCommon/);

  fs.rmSync(project, {recursive: true, force: true});
});

test('the native halves an app has installed are the ones built', () => {
  const project = scratch();
  const install = (under, name) => {
    const dir = path.join(under, 'node_modules', name);
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({name}));
    return dir;
  };
  const cmakePath = dir => dir.split(path.sep).join('/');

  // A plain React Native app builds none of them.
  assert.deepEqual(desktop.optionalNativeModules(project).args, []);

  // expo-modules-core beneath expo, where a package manager that does not
  // hoist leaves it -- found all the same.
  const expo = install(project, 'expo');
  const expoCore = install(expo, 'expo-modules-core');
  assert.deepEqual(desktop.optionalNativeModules(project).args, [
    `-DBASALT_EXPO_MODULES_CORE=${cmakePath(expoCore)}`,
  ]);

  // Reanimated without worklets is skipped with a note, not passed to a
  // configure that would refuse it.
  const reanimated = install(project, 'react-native-reanimated');
  let found = desktop.optionalNativeModules(project);
  assert.ok(!found.args.some(arg => arg.startsWith('-DBASALT_REANIMATED')));
  assert.ok(found.notes.some(note => note.includes('react-native-worklets')));

  const worklets = install(project, 'react-native-worklets');
  found = desktop.optionalNativeModules(project);
  assert.ok(found.args.includes(`-DBASALT_WORKLETS=${cmakePath(worklets)}`));
  assert.ok(found.args.includes(`-DBASALT_REANIMATED=${cmakePath(reanimated)}`));

  fs.rmSync(project, {recursive: true, force: true});
});

test('the build names clang rather than taking the system compiler', () => {
  // Linux and macOS: clang, unless the environment names a compiler itself.
  assert.deepEqual(desktop.compilerArgs('linux', {}), [
    '-DCMAKE_C_COMPILER=clang',
    '-DCMAKE_CXX_COMPILER=clang++',
  ]);
  assert.deepEqual(desktop.compilerArgs('macos', {CXX: 'g++-14'}), []);

  // Windows: clang-cl and a build type always; the compiler yields to CC/CXX,
  // the build type does not.
  const bare = desktop.compilerArgs('windows', {});
  assert.ok(bare.includes('-DCMAKE_CXX_COMPILER=clang-cl'));
  assert.ok(bare.includes('-DCMAKE_BUILD_TYPE=RelWithDebInfo'));
  const named = desktop.compilerArgs('windows', {CC: 'cl', CXX: 'cl'});
  assert.ok(!named.some(arg => arg.startsWith('-DCMAKE_CXX_COMPILER')));
  assert.ok(named.includes('-DCMAKE_BUILD_TYPE=RelWithDebInfo'));

  // vcpkg is chosen by whether its packages are installed, not by VCPKG_ROOT:
  // vcvars64.bat points that at Visual Studio's empty copy, and the one with
  // glog in it is elsewhere.
  const home = scratch();
  const empty = path.join(home, 'vs-bundled-vcpkg');
  fs.mkdirSync(path.join(empty, 'scripts', 'buildsystems'), {recursive: true});
  fs.writeFileSync(path.join(empty, 'scripts', 'buildsystems', 'vcpkg.cmake'), '');
  const real = path.join(home, 'Tools', 'vcpkg');
  fs.mkdirSync(path.join(real, 'scripts', 'buildsystems'), {recursive: true});
  fs.writeFileSync(path.join(real, 'scripts', 'buildsystems', 'vcpkg.cmake'), '');
  fs.mkdirSync(path.join(real, 'installed', 'x64-windows', 'include', 'glog'), {recursive: true});
  fs.writeFileSync(path.join(real, 'installed', 'x64-windows', 'include', 'glog', 'logging.h'), '');

  const toolchain = desktop
    .compilerArgs('windows', {VCPKG_ROOT: empty, USERPROFILE: home})
    .find(arg => arg.startsWith('-DCMAKE_TOOLCHAIN_FILE='));
  assert.equal(
    toolchain,
    `-DCMAKE_TOOLCHAIN_FILE=${path.join(real, 'scripts', 'buildsystems', 'vcpkg.cmake').split(path.sep).join('/')}`,
  );

  fs.rmSync(home, {recursive: true, force: true});
});

test('Git Bash is found beside git, and a WSL launcher is never it', () => {
  const root = scratch();
  const make = file => {
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, '');
    return file;
  };

  // Git for Windows: git.exe in <Git>\cmd, bash.exe in <Git>\bin.
  const gitCmd = path.join(root, 'Git', 'cmd');
  make(path.join(gitCmd, 'git.exe'));
  const bash = make(path.join(root, 'Git', 'bin', 'bash.exe'));
  // WSL's launcher, first on PATH the way it is on a real machine.
  const system32 = path.join(root, 'Windows', 'System32');
  make(path.join(system32, 'bash.exe'));

  const found = desktop.findGitBash({PATH: [system32, gitCmd].join(';')});
  assert.equal(found, path.normalize(bash));

  // BASALT_BASH wins, but not if it names a launcher.
  assert.equal(desktop.findGitBash({BASALT_BASH: bash, PATH: ''}), bash);
  assert.equal(
    desktop.findGitBash({BASALT_BASH: path.join(system32, 'bash.exe'), PATH: gitCmd}),
    null,
  );

  // Nothing but a launcher: no answer, rather than the wrong one.
  assert.equal(desktop.findGitBash({PATH: system32}), null);

  fs.rmSync(root, {recursive: true, force: true});
});

test('the environment vcvars prints is read as NAME=value lines only', () => {
  const parsed = desktop.parseSetOutput(
    '**********\r\n' +
      'INCLUDE=C:\SDK\include;C:\VC\include\r\n' +
      'ProgramFiles(x86)=C:\Program Files (x86)\r\n' +
      'EMPTY=\r\n' +
      '=C:=C:\\r\n',
  );
  assert.equal(parsed.INCLUDE, 'C:\SDK\include;C:\VC\include');
  assert.equal(parsed['ProgramFiles(x86)'], 'C:\Program Files (x86)');
  assert.equal(parsed.EMPTY, '');
  // cmd's hidden per-drive variables start with "=", and are not variables a
  // child process can be given.
  assert.ok(!Object.keys(parsed).some(name => name.startsWith('=')));
});

test('a Metro that dies on start is reported at once, with its own error', async () => {
  const {startMetro} = require(path.join(REPO, 'packages/react-native-basalt/cli/metro.js'));
  const project = scratch();

  // A stand-in for react-native's cli.js that fails the way Metro does in an
  // Expo app with no @react-native/metro-config: one error line, then exit.
  const reactNative = path.join(project, 'node_modules', 'react-native');
  fs.mkdirSync(reactNative, {recursive: true});
  fs.writeFileSync(
    path.join(reactNative, 'cli.js'),
    "console.error('error Cannot resolve `@react-native/metro-config`.'); process.exit(1);\n",
  );

  // A port nothing listens on, so only the child's exit can end the wait.
  const port = await new Promise(resolve => {
    const server = require('node:net').createServer();
    server.listen(0, () => {
      const {port: free} = server.address();
      server.close(() => resolve(free));
    });
  });

  const started = Date.now();
  let thrown = null;
  try {
    await startMetro({root: project, reactNativePath: reactNative}, port);
  } catch (error) {
    thrown = error;
  }
  const elapsed = Date.now() - started;

  assert.ok(thrown, 'startMetro should reject when Metro exits');
  assert.ok(elapsed < 15000, `reported after ${elapsed}ms, not at once`);
  assert.match(thrown.message, /exited before listening/);
  assert.match(thrown.message, /Cannot resolve `@react-native\/metro-config`/);

  fs.rmSync(project, {recursive: true, force: true});
});

test('a development run names the Metro config to install before starting Metro', () => {
  const project = scratch();
  const install = (name, version) => {
    const dir = path.join(project, 'node_modules', ...name.split('/'));
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({name, version}));
  };

  // What an Expo app has: react-native, and no @react-native/metro-config.
  install('react-native', '0.86.3');
  const message = desktop.missingMetroConfig(project);
  assert.ok(
    message.includes('npm install --save-dev @react-native/metro-config@0.86.3'),
    message,
  );
  assert.ok(message.includes('--mode release'));

  install('@react-native/metro-config', '0.86.3');
  assert.equal(desktop.missingMetroConfig(project), null);

  fs.rmSync(project, {recursive: true, force: true});
});

// ---------------------------------------------------------------------------
// Packaging: turning the host binary into what each desktop calls an app
// ---------------------------------------------------------------------------

const packageApp = require(
  path.join(REPO, 'packages/react-native-basalt/cli/packageApp.js'),
);

/** A throwaway project directory with the given files in it. */
function projectWith(files) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'basalt-package-'));
  for (const [name, contents] of Object.entries(files)) {
    fs.writeFileSync(path.join(root, name), contents);
  }
  return root;
}

test('an app that names itself is not given a derived identifier', () => {
  const root = projectWith({
    'app.json': JSON.stringify({
      name: 'demo',
      displayName: 'The Demo',
      basalt: {identifier: 'com.example.demo', scheme: 'demo'},
    }),
  });
  const config = packageApp.readAppConfig(root);
  assert.strictEqual(config.name, 'The Demo');
  assert.strictEqual(config.identifier, 'com.example.demo');
  assert.deepStrictEqual(config.schemes, ['demo']);
});

test('an app that names nothing still gets a usable identifier', () => {
  // Derived rather than absent, and deliberately recognisable: an app shipping
  // as com.basalt.<slug> should be able to tell that nobody chose it.
  const root = projectWith({'app.json': JSON.stringify({name: 'My App'})});
  const config = packageApp.readAppConfig(root);
  assert.strictEqual(config.identifier, 'com.basalt.my-app');
  assert.deepStrictEqual(config.schemes, []);
});

test('the Info.plist carries the identity macOS needs to notify', () => {
  const config = {
    name: 'The Demo',
    identifier: 'com.example.demo',
    schemes: ['demo', 'demo2'],
    version: '2.1.0',
  };
  const plist = packageApp.infoPlist(config, 'basalt_appkit');
  // The identifier is the whole point: without one UNUserNotificationCenter
  // raises rather than failing. See appkit/AppKitNotifications.mm.
  assert.match(plist, /<key>CFBundleIdentifier<\/key>\s*<string>com\.example\.demo<\/string>/);
  assert.match(plist, /<key>CFBundleExecutable<\/key>\s*<string>basalt_appkit<\/string>/);
  assert.match(plist, /<string>2\.1\.0<\/string>/);
  // Both schemes, so a link opens the app.
  assert.match(plist, /<string>demo<\/string>/);
  assert.match(plist, /<string>demo2<\/string>/);
});

test('a name with an ampersand in it does not produce broken XML', () => {
  const plist = packageApp.infoPlist(
    {name: 'Ben & Co', identifier: 'com.example.benco', schemes: [], version: '1.0.0'},
    'basalt_appkit',
  );
  assert.match(plist, /<string>Ben &amp; Co<\/string>/);
  assert.doesNotMatch(plist, /<string>Ben & Co<\/string>/);
});

test('packaging for macOS returns the executable inside the bundle', {
  skip: process.platform !== 'darwin' ? 'needs macOS, for codesign' : false,
}, () => {
  const root = projectWith({'app.json': JSON.stringify({name: 'demo'})});
  const binary = path.join(root, 'basalt_appkit');
  fs.writeFileSync(binary, '#!/bin/sh\nexit 0\n');
  fs.chmodSync(binary, 0o755);

  const out = path.join(root, 'build');
  const result = packageApp.packageApp({
    platform: 'macos',
    hostBinary: binary,
    outputDir: out,
    projectRoot: root,
  });

  // What is launched has to be the copy *inside* the bundle: NSBundle.mainBundle
  // comes from where the executable sits, so running the original would be
  // running an unbundled process with a bundle sitting beside it.
  assert.strictEqual(
    result.launchPath,
    path.join(out, 'demo.app', 'Contents', 'MacOS', 'basalt_appkit'),
  );
  assert.ok(fs.existsSync(path.join(out, 'demo.app', 'Contents', 'Info.plist')));
  assert.ok(fs.existsSync(result.launchPath));
});

test('packaging for Linux writes a desktop entry and launches the binary itself', () => {
  const root = projectWith({
    'app.json': JSON.stringify({name: 'demo', basalt: {identifier: 'com.example.demo', scheme: 'demo'}}),
  });
  const binary = path.join(root, 'basalt_gtk');
  fs.writeFileSync(binary, '');

  const out = path.join(root, 'build');
  const result = packageApp.packageApp({
    platform: 'linux',
    hostBinary: binary,
    outputDir: out,
    projectRoot: root,
  });

  // Unlike macOS, nothing about where the binary sits decides anything, so the
  // original is what runs.
  assert.strictEqual(result.launchPath, binary);
  const entry = fs.readFileSync(result.desktopPath, 'utf8');
  assert.match(entry, /^Name=demo$/m);
  assert.match(entry, new RegExp('^Exec=' + binary + ' %U$', 'm'));
  // The scheme, which is what makes a link open the app.
  assert.match(entry, /^MimeType=x-scheme-handler\/demo;$/m);
});

test('packaging for Windows changes nothing, because the host does it', () => {
  const root = projectWith({'app.json': JSON.stringify({name: 'demo'})});
  const binary = path.join(root, 'basalt_win32.exe');
  fs.writeFileSync(binary, '');
  const result = packageApp.packageApp({
    platform: 'windows',
    hostBinary: binary,
    outputDir: path.join(root, 'build'),
    projectRoot: root,
  });
  // A Start Menu shortcut carrying an AppUserModelID needs IPropertyStore,
  // which is COM; win32/Win32Packaging.h does it at startup instead.
  assert.strictEqual(result.launchPath, binary);
  assert.strictEqual(result.appPath, undefined);
  assert.strictEqual(result.desktopPath, undefined);
});
