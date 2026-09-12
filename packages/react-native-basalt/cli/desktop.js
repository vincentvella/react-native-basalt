/**
 * `react-native run-linux`, `run-macos` and `run-windows`, which are one
 * command with three names.
 *
 * Everything a desktop run does is the same on all three: find or build a host
 * binary, work out the module name, make sure a packager is running, write a
 * bundle, launch the host in the foreground and hand back its exit status. What
 * differs is four strings -- the platform, the command name, the binary, and
 * where its native sources are -- so that is what a platform package passes in.
 *
 * The same division as the C++ half, for the same reason. Three copies of this
 * file would be three places for `--mode release` to mean something slightly
 * different, and the first anyone would hear of it is an app that bundles
 * without assets on one desktop.
 *
 * @format
 */

'use strict';

const {spawn, spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');

const {bundleWithAssets, writeExpoAppConfig} = require('./bundleWithAssets');
const {isPortTaken, startMetro} = require('./metro');

const DEFAULT_PORT = 8081;

/**
 * A target is the whole of what a platform package contributes.
 *
 *   platform     what Metro bundles for, and what Platform.OS will say
 *   command      the CLI command name
 *   binary       the host executable, with the extension the platform uses
 *   nativeDir    that package's native/ directory, for --build
 *   coreDir      react-native-basalt/native, where bootstrap.sh lives
 *   toolchain    one sentence naming what --build needs installed
 *   generator    optional CMake generator, when the default is wrong
 */

class MissingHost extends Error {}

/**
 * Executable, on a platform that has an opinion about it.
 *
 * `X_OK` is meaningless on Windows -- every readable file answers yes -- so the
 * question there is only whether it is a file. Asking the same question of a
 * Unix binary would call a non-executable one runnable, which is worse than
 * either mistake this avoids.
 */
function isExecutable(file) {
  try {
    if (!fs.statSync(file).isFile()) {
      return false;
    }
    if (process.platform !== 'win32') {
      fs.accessSync(file, fs.constants.X_OK);
    }
    return true;
  } catch {
    return false;
  }
}

/**
 * Where to look for the host, in order of how explicit the answer is.
 *
 * The last entry is the development checkout: when the platform package is
 * being used from inside its own repository, the build tree is four
 * directories up. That is what makes the demo app in examples/ work without
 * configuration.
 */
function candidates(projectRoot, options, target) {
  const found = [];

  if (options.hostBinary) {
    found.push(path.resolve(projectRoot, options.hostBinary));
  }
  if (process.env.BASALT_HOST) {
    found.push(path.resolve(process.env.BASALT_HOST));
  }
  // Where `--build` puts one.
  found.push(path.join(projectRoot, '.basalt', 'build', target.binary));
  found.push(path.join(projectRoot, target.platform, 'build', target.binary));
  found.push(path.join(projectRoot, 'build', target.binary));
  found.push(path.join(target.nativeDir, '..', '..', '..', 'build', target.binary));

  return found.map(entry => path.normalize(entry));
}

function resolveHost(projectRoot, options, target) {
  const looked = candidates(projectRoot, options, target);
  const found = looked.find(isExecutable);
  if (found) {
    return found;
  }

  throw new MissingHost(
    `could not find ${target.binary}.\n\n` +
      'Looked in:\n' +
      looked.map(entry => `  ${entry}`).join('\n') +
      '\n\n' +
      'Build one with:\n\n' +
      `  react-native ${target.command} --build\n\n` +
      `That needs a React Native source checkout, cmake, ninja, and ${target.toolchain}. ` +
      'Or build it\nyourself and point at it with --host-binary <path> or BASALT_HOST.',
  );
}

function run(command, args, options, toolchain) {
  const result = spawnSync(command, args, {stdio: 'inherit', ...options});
  if (result.error) {
    if (result.error.code === 'ENOENT') {
      throw new Error(
        `${command} is not installed. Building the host needs cmake, ninja, and ${toolchain}.`,
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

/**
 * Building the host from an app.
 *
 * This is what `run-android` gets from Gradle and `run-ios` from Xcode. There
 * is no equivalent to hand it to, so the work is here: vendor React Native's
 * C++ dependencies, configure CMake against the app's own React Native, and
 * build.
 *
 * Two things about where output goes. It goes under the app, in `.basalt`, not
 * into `node_modules`, because npm rewrites node_modules on install and a
 * Hermes build is not something to lose that way. And `third_party` is shared
 * between builds of the same app rather than per configuration, because it
 * depends on the React Native version and nothing else; the version stamp
 * inside it is what keeps that honest.
 *
 * The first build is slow in a way `run-android` is not. It compiles Hermes and
 * React Native's C++ core. Saying so before it starts is better than a silent
 * half hour.
 */
function buildHost(context, options, target) {
  const projectRoot = context.root;
  const workDir = path.join(projectRoot, '.basalt');
  const buildDir = path.join(workDir, 'build');
  const thirdParty = path.join(workDir, 'third_party');

  const reactNativeRoot = monorepoRoot(context.reactNativePath);
  if (reactNativeRoot == null) {
    throw new Error(
      'building the host needs a React Native source checkout, and this project ' +
        `resolves react-native to ${context.reactNativePath}, which is an ` +
        'installed package rather than a checkout.\n\n' +
        'That is a real limitation, not a misconfiguration: the host is compiled ' +
        "against React Native's C++ sources, and the npm package does not ship " +
        'them. Point at a checkout of the same version with --react-native-path, ' +
        'or build the host yourself and pass --host-binary.',
    );
  }

  fs.mkdirSync(workDir, {recursive: true});

  console.log("==> vendoring React Native's C++ dependencies");
  console.log('    The first run compiles Hermes and takes a while.');
  const bootstrap = path.join(target.coreDir, 'bootstrap.sh');
  // Through bash by name on Windows, where a .sh file is not executable and
  // there is no shebang handling -- and where the shell that runs it is Git
  // Bash, which is what bootstrap.sh detects and expects.
  const [runner, runnerArgs] =
    process.platform === 'win32' ? ['bash', [bootstrap]] : [bootstrap, []];
  run(
    runner,
    [...runnerArgs, reactNativeRoot],
    {cwd: workDir, env: {...process.env, BASALT_THIRD_PARTY: thirdParty}},
    target.toolchain,
  );

  console.log('==> configuring');
  run(
    'cmake',
    [
      '-S',
      target.nativeDir,
      '-B',
      buildDir,
      '-G',
      target.generator ?? 'Ninja',
      `-DRN_DIR=${path.join(reactNativeRoot, 'packages', 'react-native')}`,
      `-DBASALT_THIRD_PARTY=${thirdParty}`,
      // Where the shared half is. CMake has a default that covers a checkout
      // and a hoisted node_modules, and naming it here covers pnpm's store too,
      // where the two packages are not siblings on disk at all.
      `-DBASALT_CORE_DIR=${target.coreDir}`,
      ...(target.configureArgs ?? []),
    ],
    {},
    target.toolchain,
  );

  console.log('==> building');
  const jobs = options.jobs ? ['-j', String(options.jobs)] : [];
  run('cmake', ['--build', buildDir, ...jobs], {}, target.toolchain);

  const binary = path.join(buildDir, target.binary);
  if (!isExecutable(binary)) {
    throw new Error(`the build produced no ${target.binary} at ${binary}`);
  }
  return binary;
}

/**
 * The name the app registered with AppRegistry.
 *
 * app.json's `name`, which is what `registerRootComponent` and every template
 * use, and what run-android reads for the same purpose. Expo apps register
 * "main" regardless, so an explicit --module has to win.
 */
function resolveModuleName(projectRoot, options) {
  if (options.module) {
    return options.module;
  }
  const appJson = path.join(projectRoot, 'app.json');
  if (fs.existsSync(appJson)) {
    try {
      const parsed = JSON.parse(fs.readFileSync(appJson, 'utf8'));
      if (typeof parsed.name === 'string' && parsed.name.length > 0) {
        return parsed.name;
      }
    } catch (error) {
      throw new Error(`could not read ${appJson}: ${error.message}`);
    }
  }
  throw new Error(
    'no module name: add a "name" to app.json, or pass --module <name>.\n' +
      'It is the name the app passes to AppRegistry.registerComponent.',
  );
}

/**
 * One bundler for every app.
 *
 * React Native's CLI could do this, and did, but only an app that has that CLI
 * can reach it -- an Expo app cannot. Since the same code has to exist for
 * them, it may as well be the only path, so both kinds of app get identical
 * output and there is one place where assets can go wrong.
 */
async function bundleForRelease(context, options, outputPath, target) {
  const result = await bundleWithAssets({
    projectRoot: context.root,
    entryFile: options.entryFile,
    bundleOutput: outputPath,
    platform: target.platform,
    dev: false,
  });
  console.log(
    `    ${result.files} asset file${result.files === 1 ? '' : 's'} beside the bundle`,
  );
}

/**
 * Runs the host in the foreground.
 *
 * Foreground on purpose: this is a desktop application, and the terminal that
 * launched it is where its output belongs and where Ctrl-C should reach it.
 */
function launch(hostBinary, {bundlePath, moduleName, dev, port, entry, cwd}) {
  const env = {...process.env};
  if (dev) {
    env.BASALT_DEV = '1';
    env.BASALT_DEV_PORT = String(port);
    env.BASALT_DEV_ENTRY = entry;
  } else {
    delete env.BASALT_DEV;
  }

  return new Promise((resolve, reject) => {
    const child = spawn(hostBinary, [bundlePath, moduleName], {
      cwd,
      env,
      stdio: 'inherit',
    });
    child.on('error', reject);
    child.on('exit', (code, signal) => {
      // A signal is how a windowed app normally ends here, and Ctrl-C reaches
      // the child directly because it shares this terminal's process group.
      resolve(signal != null ? 0 : (code ?? 0));
    });
  });
}

async function runDesktop(_argv, context, options, target) {
  const projectRoot = context.root;
  const port = Number(options.port) || DEFAULT_PORT;
  const dev = options.mode !== 'release';

  // --build is explicit rather than automatic. A first build compiles Hermes
  // and React Native's C++ core, which is not something a command should start
  // on its own because a binary happened to be missing.
  let hostBinary;
  if (options.build) {
    hostBinary = buildHost(context, options, target);
  } else {
    hostBinary = resolveHost(projectRoot, options, target);
  }
  const moduleName = resolveModuleName(projectRoot, options);

  // Named relative to the project so that the message is readable, but passed
  // to the host as an absolute path, since it runs with the project as its
  // working directory and a relative bundle would resolve differently.
  const bundlePath = path.resolve(
    projectRoot,
    options.bundle ?? path.join('build', 'main.jsbundle.js'),
  );

  if (dev) {
    if (options.packager === false) {
      console.log(`==> not starting Metro; expecting one on port ${port}`);
    } else if (await isPortTaken(port)) {
      console.log(`==> reusing the packager already on port ${port}`);
    } else {
      const {logPath} = await startMetro(context, port);
      console.log(
        `==> started Metro on port ${port}, logging to ${path.relative(projectRoot, logPath)}`,
      );
    }

    // Still needed in development: the host falls back to this file when Metro
    // cannot be reached, and a missing one turns a packager problem into a
    // confusing failure to load anything at all.
    if (!fs.existsSync(bundlePath)) {
      console.log('==> writing a fallback bundle for when Metro is unreachable');
      fs.mkdirSync(path.dirname(bundlePath), {recursive: true});
      await bundleForRelease(context, options, bundlePath, target);
    }

    // Every dev run, not only the one that wrote the fallback bundle: the host
    // reads app.config.json from beside the bundle, and an app.json edited
    // since would otherwise be invisible in development and applied in release
    // -- the worse half of a difference between the two.
    await writeExpoAppConfig(projectRoot, bundlePath);
  } else {
    console.log('==> bundling for release');
    await bundleForRelease(context, options, bundlePath, target);
  }

  console.log(`==> ${path.basename(hostBinary)} ${moduleName}${dev ? ' (dev)' : ''}`);
  const status = await launch(hostBinary, {
    bundlePath,
    moduleName,
    dev,
    port,
    entry: options.entryFile.replace(/\.[^.]+$/, ''),
    cwd: projectRoot,
  });
  if (status !== 0) {
    throw new Error(`the app exited with status ${status}`);
  }
}

/**
 * The CLI command object React Native's CLI expects, for one target.
 */
function makeRunCommand(target) {
  return {
    name: target.command,
    description: `builds nothing and runs your app on ${target.label}, against a packager`,
    func: async (argv, context, options) => {
      try {
        await runDesktop(argv, context, options, target);
      } catch (error) {
        if (error instanceof MissingHost) {
          // Its message is the instructions; a stack trace on top would bury
          // them.
          console.error(`\nerror: ${error.message}\n`);
          process.exitCode = 1;
          return;
        }
        throw error;
      }
    },
    options: [
      {
        name: '--port <number>',
        description: 'the port Metro is or should be on',
        default: DEFAULT_PORT,
      },
      {
        name: '--mode <string>',
        description: '"dev" to run against Metro, "release" to run a bundle',
        default: 'dev',
      },
      {
        name: '--no-packager',
        description: 'do not start Metro, and do not check for one',
      },
      {
        name: '--build',
        description: 'build the host before running it, into .basalt/build',
      },
      {
        name: '--jobs <number>',
        description: 'parallelism for --build',
      },
      {
        name: '--host-binary <path>',
        description: `the ${target.binary} to run, if it is not somewhere obvious`,
      },
      {
        name: '--module <string>',
        description: "the AppRegistry name to start, if it is not app.json's",
      },
      {
        name: '--bundle <path>',
        description: 'where the release bundle is written and read',
      },
      {
        name: '--entry-file <path>',
        description: 'the app entry point',
        default: 'index.js',
      },
    ],
  };
}

module.exports = {
  MissingHost,
  buildHost,
  candidates,
  isExecutable,
  makeRunCommand,
  monorepoRoot,
  resolveHost,
  resolveModuleName,
};
