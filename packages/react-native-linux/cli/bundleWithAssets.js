/**
 * Bundling for linux, assets included.
 *
 * React Native's own CLI does this and takes `--assets-dest`, so a React Native
 * app is served by `run-linux`. An Expo app is not: its CLI is `expo`, which
 * does not know this platform, and Metro's own `build` command has no asset
 * option at all. Without one, `require('./logo.png')` resolves to a path beside
 * the bundle that nothing ever wrote.
 *
 * So this is the same two steps React Native's CLI performs internally: build
 * the bundle, then ask Metro which assets the build referenced and copy each
 * one where resolution will look for it.
 *
 * @format
 */

'use strict';

const fs = require('fs');
const path = require('path');
const {createRequire} = require('module');

/**
 * Metro, and the pieces of it that have no public export.
 *
 * `metro/private/*` is how Metro's own package.json exposes its internals, and
 * `Server.getAssets` is what React Native's CLI uses for exactly this. Resolved
 * from the project rather than from here, so an app gets the Metro it installed
 * and not a second copy.
 */
function loadMetro(projectRoot) {
  const resolvers = [createRequire(path.join(projectRoot, 'package.json'))];

  // Then from React Native's own location. An installed app has Metro hoisted
  // into its node_modules and the first resolver finds it; a checkout, a
  // workspace or a linked package may not, and Metro is always beside React
  // Native. Looked up second rather than first so an app that pins its own
  // Metro gets the one it pinned.
  try {
    resolvers.push(createRequire(resolvers[0].resolve('react-native/package.json')));
  } catch {
    // No react-native from the project. The error below will be the useful one.
  }

  for (const resolve of resolvers) {
    try {
      const metro = resolve('metro');
      const serverModule = resolve('metro/private/Server');
      return {metro, Server: serverModule.default ?? serverModule};
    } catch {
      // Try the next one.
    }
  }

  throw new Error(
    `could not find Metro from ${projectRoot} or from its react-native. ` +
      'Bundling needs it; it is normally installed with react-native.',
  );
}

/**
 * Where one asset file belongs, relative to the assets directory.
 *
 * This has to agree exactly with what React Native computes at runtime in
 * `AssetSourceResolver.scaledAssetPath`, or assets land somewhere resolution
 * does not look and the failure is a missing file with no explanation. The
 * `../` replacement is React Native's: an asset can sit outside the project
 * root, and its path must not escape the assets directory.
 */
function destinationFor(asset, scale) {
  const suffix = scale === 1 ? '' : `@${scale}x`;
  const name = `${asset.name}${suffix}.${asset.type}`;
  return path.join(asset.httpServerLocation.replace(/^\//, '').replace(/\.\.\//g, '_'), name);
}

async function copyAssets(assets, assetsDest) {
  let copied = 0;
  for (const asset of assets) {
    // `scales` and `files` are parallel arrays: one file per scale.
    for (let index = 0; index < asset.scales.length; index++) {
      const source = asset.files[index];
      if (source == null) {
        continue;
      }
      const destination = path.join(assetsDest, destinationFor(asset, asset.scales[index]));
      await fs.promises.mkdir(path.dirname(destination), {recursive: true});
      await fs.promises.copyFile(source, destination);
      copied++;
    }
  }
  return copied;
}

/**
 * Builds `entryFile` to `bundleOutput`, and writes its assets beside it.
 *
 * Beside it by default, and that default is not arbitrary: React Native
 * resolves an asset relative to the script's own location, so anywhere else is
 * wrong unless the script is somewhere else too.
 */
async function bundleWithAssets({
  projectRoot,
  entryFile,
  bundleOutput,
  assetsDest,
  platform = 'linux',
  dev = false,
  minify = !dev,
  configPath,
}) {
  const {metro, Server} = loadMetro(projectRoot);

  const config = await metro.loadConfig({cwd: projectRoot, config: configPath});
  const destination = assetsDest ?? path.dirname(bundleOutput);

  await fs.promises.mkdir(path.dirname(bundleOutput), {recursive: true});
  await metro.runBuild(config, {
    entry: entryFile,
    out: bundleOutput,
    platform,
    dev,
    minify,
  });

  // A second Metro over the same config. Wasteful-looking and not: the build
  // above discards the graph, and this is the only public-ish way to ask which
  // assets it contained.
  const server = new Server(config);
  try {
    const assets = await server.getAssets({
      ...Server.DEFAULT_BUNDLE_OPTIONS,
      entryFile,
      platform,
      dev,
      minify,
      bundleType: 'todo',
    });
    const copied = await copyAssets(assets, destination);
    return {assets: assets.length, files: copied, assetsDest: destination};
  } finally {
    server.end();
  }
}

module.exports = {bundleWithAssets, destinationFor};
