/**
 * `react-native run-linux`.
 *
 * Does what `run-android` does, minus the build: makes sure a packager is
 * running, then launches the app and stays attached to it, so Ctrl-C stops the
 * app and the exit status is the app's.
 *
 * The host takes its bundle over http in development and from a file in
 * release, and it is told which by RN_LINUX_DEV. In release the bundle has to
 * exist first, so this writes one.
 *
 * @format
 */

'use strict';

const {spawn} = require('child_process');
const fs = require('fs');
const path = require('path');

const {buildHost} = require('./build');
const {MissingHost, resolveHost} = require('./host');
const {isPortTaken, startMetro} = require('./metro');

const DEFAULT_PORT = 8081;

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

function bundleForRelease(context, options, outputPath) {
  const cli = path.join(context.reactNativePath, 'cli.js');
  const args = [
    cli,
    'bundle',
    '--platform',
    'linux',
    '--dev',
    'false',
    '--entry-file',
    options.entryFile,
    '--bundle-output',
    outputPath,
  ];

  const result = require('child_process').spawnSync(process.execPath, args, {
    cwd: context.root,
    stdio: 'inherit',
  });
  if (result.status !== 0) {
    throw new Error('bundling failed');
  }
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
    env.RN_LINUX_DEV = '1';
    env.RN_LINUX_DEV_PORT = String(port);
    env.RN_LINUX_DEV_ENTRY = entry;
  } else {
    delete env.RN_LINUX_DEV;
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

async function runLinux(_argv, context, options) {
  const projectRoot = context.root;
  const port = Number(options.port) || DEFAULT_PORT;
  const dev = options.mode !== 'release';

  // --build is explicit rather than automatic. A first build compiles Hermes
  // and React Native's C++ core, which is not something a command should start
  // on its own because a binary happened to be missing.
  let hostBinary;
  if (options.build) {
    hostBinary = buildHost(context, options);
  } else {
    hostBinary = resolveHost(projectRoot, options);
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
      console.log(`==> started Metro on port ${port}, logging to ${path.relative(projectRoot, logPath)}`);
    }

    // Still needed in development: the host falls back to this file when Metro
    // cannot be reached, and a missing one turns a packager problem into a
    // confusing failure to load anything at all.
    if (!fs.existsSync(bundlePath)) {
      console.log('==> writing a fallback bundle for when Metro is unreachable');
      fs.mkdirSync(path.dirname(bundlePath), {recursive: true});
      bundleForRelease(context, options, bundlePath);
    }
  } else {
    console.log('==> bundling for release');
    fs.mkdirSync(path.dirname(bundlePath), {recursive: true});
    bundleForRelease(context, options, bundlePath);
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

module.exports = {
  name: 'run-linux',
  description: 'builds nothing and runs your app on Linux, against a packager',
  func: async (argv, context, options) => {
    try {
      await runLinux(argv, context, options);
    } catch (error) {
      if (error instanceof MissingHost) {
        // Its message is the instructions; a stack trace on top would bury them.
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
      description: 'build the host before running it, into .rn-linux/build',
    },
    {
      name: '--jobs <number>',
      description: 'parallelism for --build',
    },
    {
      name: '--host-binary <path>',
      description: 'the rn_linux_host to run, if it is not somewhere obvious',
    },
    {
      name: '--module <string>',
      description: 'the AppRegistry name to start, if it is not app.json\'s',
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
