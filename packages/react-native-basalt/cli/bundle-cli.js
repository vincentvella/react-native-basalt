#!/usr/bin/env node
/**
 * `basalt-bundle`, for apps that cannot use React Native's CLI.
 *
 * An Expo app has `expo` as its CLI, which does not know this platform, and
 * Metro's own `build` emits no assets. This is the missing command: one bundle,
 * its assets beside it, ready for the host to load.
 *
 * @format
 */

'use strict';

const path = require('path');

const {bundleWithAssets} = require('./bundleWithAssets');

function parse(argv) {
  const options = {
    projectRoot: process.cwd(),
    entryFile: 'index.js',
    platform: 'linux',
    dev: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const next = () => argv[++i];
    switch (argv[i]) {
      case '--project-root': options.projectRoot = path.resolve(next()); break;
      case '--entry-file': options.entryFile = next(); break;
      case '--bundle-output': options.bundleOutput = path.resolve(next()); break;
      case '--assets-dest': options.assetsDest = path.resolve(next()); break;
      case '--platform': options.platform = next(); break;
      case '--config': options.configPath = path.resolve(next()); break;
      case '--dev': options.dev = next() !== 'false'; break;
      case '--minify': options.minify = next() !== 'false'; break;
      case '-h':
      case '--help': options.help = true; break;
      default:
        throw new Error(`unknown option ${argv[i]}`);
    }
  }
  return options;
}

const USAGE = `
basalt-bundle --bundle-output <path> [options]

  --entry-file <path>     default index.js
  --bundle-output <path>  required
  --assets-dest <dir>     default: beside the bundle, which is where React
                          Native looks for assets at runtime
  --project-root <dir>    default: the working directory
  --config <path>         a metro.config.js, if not the project's own
  --dev true|false        default false
  --minify true|false     default: the opposite of --dev
`;

async function main() {
  const options = parse(process.argv.slice(2));
  if (options.help || options.bundleOutput == null) {
    console.log(USAGE.trim());
    process.exitCode = options.help ? 0 : 1;
    return;
  }

  const result = await bundleWithAssets(options);
  console.log(
    `wrote ${path.relative(options.projectRoot, options.bundleOutput)} ` +
      `and ${result.files} asset file${result.files === 1 ? '' : 's'} ` +
      `from ${result.assets} asset${result.assets === 1 ? '' : 's'}`,
  );
}

main().catch(error => {
  console.error(`error: ${error.message}`);
  process.exitCode = 1;
});
