#!/usr/bin/env node
/**
 * `npx react-native-basalt init`, which adds a desktop to an app that exists.
 *
 * It does not create an app. `create-expo-app` does that, and the division is
 * the same one `run-linux` already assumes: this platform is something you add
 * to a React Native app, not a way to start one.
 *
 * ## Why a bin rather than a CLI command
 *
 * The run commands are React Native CLI commands, registered from the host
 * packages, because the CLI has to be able to find them. This one cannot be:
 * before it runs, the package is not a dependency yet, so there is no config
 * for the CLI to read. `npx react-native-basalt init` works from nothing, which
 * is the point -- one command, in an app that has never heard of this platform.
 *
 * ## Idempotent, and refusing beats half-configuring
 *
 * Every step reports one of three things: it was already right, it was changed,
 * or it could not be done and why. Running twice is safe and says so. An app
 * this cannot recognise is refused before anything is written, because a
 * half-configured app fails later and further from the cause than one that was
 * turned away -- and the failure it produces names a Metro or Babel internal
 * rather than the step that was skipped.
 *
 * @format
 */

'use strict';

const fs = require('fs');
const path = require('path');

const {DESKTOP_PLATFORMS} = require('../metro-config');

// The dev dependencies an app needs to bundle for a desktop. Both are React
// Native's own: the Metro config this platform wraps, and the CLI that carries
// the run commands. An Expo app has neither, because `expo` is its CLI.
const DEV_DEPENDENCIES = ['@react-native/metro-config', '@react-native-community/cli'];

const PACKAGE_NAME = 'react-native-basalt';

// What a step did, so the summary can be written once at the end rather than
// printed as it goes -- a run that refuses halfway should not have narrated
// four successes first.
const DONE = 'done';
const CHANGED = 'changed';
const BLOCKED = 'blocked';

function step(state, message) {
  return {state, message};
}

/**
 * Reads a JSON file, or returns null when it is absent or unparseable.
 *
 * Unparseable and absent are deliberately the same answer here: both mean "this
 * is not something to edit", and the caller says so with the path.
 */
function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    return null;
  }
}

/**
 * Is this a React Native app at all?
 *
 * `react-native` in any dependency field, or `expo` -- an Expo app has React
 * Native transitively and may not name it. Anything else is refused.
 */
function describeProject(root) {
  const manifest = path.join(root, 'package.json');
  if (!fs.existsSync(manifest)) {
    return {ok: false, reason: `no package.json in ${root}`};
  }
  const json = readJson(manifest);
  if (json == null) {
    return {ok: false, reason: `${manifest} is not readable JSON`};
  }
  const all = {
    ...(json.dependencies || {}),
    ...(json.devDependencies || {}),
    ...(json.peerDependencies || {}),
  };
  if (all['react-native'] == null && all.expo == null) {
    return {
      ok: false,
      reason:
        `${manifest} lists neither react-native nor expo. This adds a desktop ` +
        'to an app that exists; create one with `npx create-expo-app` first.',
    };
  }
  return {ok: true, manifest, json, expo: all.expo != null};
}

/**
 * The Metro config to edit, or null with the reason.
 *
 * Only the two filenames Metro itself looks for. A project using
 * `metro.config.ts` is a real case and not one this writes into blindly: the
 * wrapper is a require() call, and a TypeScript config may be doing something
 * this cannot preserve.
 */
function findMetroConfig(root) {
  for (const name of ['metro.config.js', 'metro.config.cjs']) {
    const file = path.join(root, name);
    if (fs.existsSync(file)) {
      return {file};
    }
  }
  for (const name of ['metro.config.ts', 'metro.config.mjs']) {
    if (fs.existsSync(path.join(root, name))) {
      return {
        file: null,
        reason:
          `found ${name}, which this cannot edit safely. Add ` +
          `\`withDesktopPlatforms\` from \`${PACKAGE_NAME}/metro-config\` by hand.`,
      };
    }
  }
  return {file: null, reason: 'no metro.config.js'};
}

/** Adds this package and the two dev dependencies, without touching versions. */
function addDependencies(project, version) {
  const json = project.json;
  const changes = [];

  json.dependencies = json.dependencies || {};
  if (json.dependencies[PACKAGE_NAME] == null) {
    json.dependencies[PACKAGE_NAME] = version;
    changes.push(PACKAGE_NAME);
  }

  json.devDependencies = json.devDependencies || {};
  for (const name of DEV_DEPENDENCIES) {
    // Not if it is already a dependency of any kind: an app that has it in
    // `dependencies` made that choice, and moving it is not this command's
    // business.
    const present =
      json.devDependencies[name] != null || (json.dependencies || {})[name] != null;
    if (!present) {
      json.devDependencies[name] = '*';
      changes.push(name);
    }
  }

  if (changes.length === 0) {
    return step(DONE, 'dependencies are already present');
  }
  return step(CHANGED, `added ${changes.join(', ')} -- run your package manager to install`);
}

/** Adds a script per desktop, leaving any the app already defined alone. */
function addScripts(project) {
  const json = project.json;
  json.scripts = json.scripts || {};
  const added = [];
  for (const platform of DESKTOP_PLATFORMS) {
    const name = platform;
    if (json.scripts[name] == null) {
      json.scripts[name] = `react-native run-${platform}`;
      added.push(name);
    }
  }
  if (added.length === 0) {
    return step(DONE, 'scripts are already present');
  }
  return step(CHANGED, `added scripts: ${added.join(', ')}`);
}

/**
 * Wraps the app's Metro config, preserving what is there.
 *
 * Textual rather than by parsing: a Metro config is a small file people edit,
 * and rewriting it through an AST would reformat the whole thing to change one
 * line. What that costs is certainty, so anything not recognised is refused
 * rather than guessed at.
 */
function wrapMetroConfig(file) {
  const before = fs.readFileSync(file, 'utf8');

  if (before.includes('withDesktopPlatforms')) {
    return step(DONE, `${path.basename(file)} already wraps the config`);
  }

  const requireLine =
    `const {withDesktopPlatforms} = require('${PACKAGE_NAME}/metro-config');\n`;

  // `module.exports = <something>;` on one line, which is what both the Expo
  // and the bare template produce.
  const assignment = /^module\.exports\s*=\s*([\s\S]+?);\s*$/m;
  const match = before.match(assignment);
  if (match == null) {
    return step(
      BLOCKED,
      `could not find a \`module.exports =\` in ${path.basename(file)}. Wrap ` +
        'its export in `withDesktopPlatforms(...)` by hand.',
    );
  }

  const wrapped = before.replace(
    assignment,
    `module.exports = withDesktopPlatforms(${match[1].trim()});`,
  );

  // After the last require at the top, so the file still reads top to bottom.
  const requires = [...wrapped.matchAll(/^(?:const|let|var)\s.*require\(.*\);\s*$/gm)];
  const after =
    requires.length > 0
      ? wrapped.slice(0, requires[requires.length - 1].index + requires[requires.length - 1][0].length) +
        '\n' +
        requireLine +
        wrapped.slice(requires[requires.length - 1].index + requires[requires.length - 1][0].length)
      : requireLine + wrapped;

  fs.writeFileSync(file, after);
  return step(CHANGED, `wrapped ${path.basename(file)} in withDesktopPlatforms`);
}

function ownVersion() {
  const json = readJson(path.join(__dirname, '..', 'package.json'));
  return json != null && json.version != null ? `^${json.version}` : '*';
}

function init(root = process.cwd(), {write = true} = {}) {
  const project = describeProject(root);
  if (!project.ok) {
    return {ok: false, reason: project.reason, steps: []};
  }

  const metro = findMetroConfig(root);
  const steps = [];

  steps.push(['dependencies', addDependencies(project, ownVersion())]);
  steps.push(['scripts', addScripts(project)]);

  if (metro.file == null) {
    steps.push(['metro config', step(BLOCKED, metro.reason)]);
  } else if (write) {
    steps.push(['metro config', wrapMetroConfig(metro.file)]);
  }

  if (write) {
    // Two spaces and a trailing newline, which is what npm itself writes, so an
    // app's package.json does not churn in the next unrelated diff.
    fs.writeFileSync(project.manifest, `${JSON.stringify(project.json, null, 2)}\n`);
  }

  return {ok: true, steps};
}

function main(argv) {
  const root = argv[0] != null && !argv[0].startsWith('-') ? path.resolve(argv[0]) : process.cwd();

  const result = init(root);
  if (!result.ok) {
    process.stderr.write(`react-native-basalt init: ${result.reason}\n`);
    return 1;
  }

  let changed = 0;
  let blocked = 0;
  for (const [name, outcome] of result.steps) {
    const mark = outcome.state === DONE ? '=' : outcome.state === CHANGED ? '+' : '!';
    process.stdout.write(`  ${mark} ${name}: ${outcome.message}\n`);
    if (outcome.state === CHANGED) changed++;
    if (outcome.state === BLOCKED) blocked++;
  }

  if (blocked > 0) {
    process.stdout.write('\nSome steps need doing by hand; see above.\n');
    return 1;
  }
  process.stdout.write(
    changed === 0
      ? '\nAlready configured. Nothing to do.\n'
      : '\nInstall dependencies, then `npm run linux` (or macos, or windows).\n',
  );
  return 0;
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}

module.exports = {init, DEV_DEPENDENCIES};
