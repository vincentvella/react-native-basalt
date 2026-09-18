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

import * as fs from 'node:fs';
import * as path from 'node:path';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const {DESKTOP_PLATFORMS} = require('../metro-config') as {
  DESKTOP_PLATFORMS: ReadonlyArray<string>;
};

// The dev dependencies an app needs to bundle for a desktop. Both are React
// Native's own: the Metro config this platform wraps, and the CLI that carries
// the run commands. An Expo app has neither, because `expo` is its CLI.
export const DEV_DEPENDENCIES = [
  '@react-native/metro-config',
  '@react-native-community/cli',
] as const;

const PACKAGE_NAME = 'react-native-basalt';

/** What one step did. The summary is written once, at the end. */
export type StepState = 'done' | 'changed' | 'blocked';

export type Step = {
  state: StepState;
  message: string;
};

/** The app's package.json, as far as this reads and writes it. */
type Manifest = {
  dependencies?: Record<string, string>;
  devDependencies?: Record<string, string>;
  peerDependencies?: Record<string, string>;
  scripts?: Record<string, string>;
  [key: string]: unknown;
};

type Project =
  | {ok: false; reason: string}
  | {ok: true; manifest: string; json: Manifest; expo: boolean};

export type InitResult =
  | {ok: false; reason: string; steps: Array<[string, Step]>}
  | {ok: true; steps: Array<[string, Step]>};

// What a step did, so the summary can be written once at the end rather than
// printed as it goes -- a run that refuses halfway should not have narrated
// four successes first.
const DONE: StepState = 'done';
const CHANGED: StepState = 'changed';
const BLOCKED: StepState = 'blocked';

function step(state: StepState, message: string): Step {
  return {state, message};
}

/**
 * Reads a JSON file, or returns null when it is absent or unparseable.
 *
 * Unparseable and absent are deliberately the same answer here: both mean "this
 * is not something to edit", and the caller says so with the path.
 */
function readJson<T = Manifest>(file: string): T | null {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8')) as T;
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
function describeProject(root: string): Project {
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
function findMetroConfig(root: string): {file: string | null; reason?: string} {
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
function addDependencies(project: Extract<Project, {ok: true}>, version: string): Step {
  const json = project.json;
  const changes: string[] = [];

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
function addScripts(project: Extract<Project, {ok: true}>): Step {
  const json = project.json;
  json.scripts = json.scripts || {};
  const added: string[] = [];
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
function wrapMetroConfig(file: string): Step {
  const before = fs.readFileSync(file, 'utf8');

  if (before.includes('withDesktopPlatforms')) {
    return step(DONE, `${path.basename(file)} already wraps the config`);
  }

  const requireLine =
    `const {withDesktopPlatforms} = require('${PACKAGE_NAME}/metro-config');\n`;

  // `module.exports = <something>;` on one line, which is what both the Expo
  // and the bare template produce.
  // Non-greedy up to the semicolon, and deliberately not swallowing what
  // follows it: `\s*$` would eat the file's trailing newline, and a command
  // that reformats the end of a file it was asked to edit one line of is a
  // command people stop trusting.
  const assignment = /^module\.exports\s*=\s*([\s\S]+?);$/m;
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

/**
 * This package's own version, for the dependency `init` adds.
 *
 * Found by walking up rather than by a fixed number of `..`: this file runs
 * from `dist/cli/` once built and from `cli/` in a checkout, so any hardcoded
 * depth is wrong in one of the two -- which is exactly the bug that shipped the
 * first time, as a dependency pinned to `*`.
 */
function ownVersion(): string {
  let dir = __dirname;
  for (let up = 0; up < 5; up++) {
    const candidate = path.join(dir, 'package.json');
    const json = readJson<{name?: string; version?: string}>(candidate);
    if (json?.name === PACKAGE_NAME) {
      return json.version != null ? `^${json.version}` : '*';
    }
    const parent = path.dirname(dir);
    if (parent === dir) {
      break;
    }
    dir = parent;
  }
  return '*';
}

export function init(root: string = process.cwd(), {write = true}: {write?: boolean} = {}): InitResult {
  const project = describeProject(root);
  if (!project.ok) {
    return {ok: false, reason: project.reason, steps: []};
  }

  const metro = findMetroConfig(root);
  const steps: Array<[string, Step]> = [];

  steps.push(['dependencies', addDependencies(project, ownVersion())]);
  steps.push(['scripts', addScripts(project)]);

  if (metro.file == null) {
    steps.push(['metro config', step(BLOCKED, metro.reason ?? 'no metro config')]);
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

function main(argv: string[]): number {
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
