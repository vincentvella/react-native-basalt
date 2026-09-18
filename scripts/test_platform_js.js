/**
 * Tests for react-native-basalt's JavaScript that does not need React Native to
 * run: today, the title bar's request stack.
 *
 * Run with:  node --test scripts/test_platform_js.js
 *
 * @format
 */

'use strict';

const assert = require('node:assert');
const path = require('node:path');
const {test} = require('node:test');

// The built output, not the source: this is TypeScript now, and `main` points
// into `dist/`. Run scripts/build_ts.sh first -- which is what the failure
// below says, because "cannot find module" on a path that plainly exists in the
// repository is the most confusing way to learn it.
const built = path.join(
  __dirname,
  '..',
  'packages/react-native-basalt/dist/src/titleBarState.js',
);
if (!require('node:fs').existsSync(built)) {
  throw new Error(`${built} does not exist. Run scripts/build_ts.sh first.`);
}
const {createTitleBarStack, sameRequest} = require(built);

test('the most recently mounted title bar request wins, key by key', () => {
  const stack = createTitleBarStack();
  stack.push({title: 'App', backgroundColor: '#111111', style: 'hidden'});
  stack.push({title: 'Settings'});

  // The screen's title, over the shell's colour and style, which it did not
  // mention and so does not reset.
  assert.deepEqual(stack.resolve(), {
    title: 'Settings',
    backgroundColor: '#111111',
    style: 'hidden',
  });
});

test("unmounting a request restores what was beneath it", () => {
  const stack = createTitleBarStack();
  stack.push({title: 'App'});
  const screen = stack.push({title: 'Settings', textColor: 'white'});

  stack.remove(screen);
  assert.deepEqual(stack.resolve(), {title: 'App'});

  stack.remove(screen); // twice is harmless
  assert.equal(stack.size, 1);
});

test('updating a request replaces its keys rather than adding to them', () => {
  const stack = createTitleBarStack();
  const id = stack.push({title: 'Inbox', backgroundColor: 'red'});

  // The component re-rendered without a background colour: it no longer asks
  // for one, so none is resolved.
  stack.update(id, {title: 'Inbox (3)'});
  assert.deepEqual(stack.resolve(), {title: 'Inbox (3)'});
});

test('keys that are not title bar options are ignored', () => {
  const stack = createTitleBarStack();
  stack.push({title: 'App', children: 'nope', onPress() {}});
  assert.deepEqual(stack.resolve(), {title: 'App'});
});

test('two requests for the same thing are the same request', () => {
  assert.ok(sameRequest({title: 'A', style: 'hidden'}, {style: 'hidden', title: 'A'}));
  assert.ok(!sameRequest({title: 'A'}, {title: 'B'}));
  assert.ok(sameRequest({}, null));
});

// --------------------------------------------------------------------------
// The package's require()-able surface
// --------------------------------------------------------------------------
//
// Every one of these is reached by `require()` from a file the type checker
// never sees -- an app's metro.config.js, this package's own
// react-native.config.js, the host packages' CLI. So dropping one compiles
// cleanly and fails at runtime somewhere else, which is exactly what happened
// once: converting metro-config.js to TypeScript replaced its `module.exports`
// block, and five of its eight exports went with it. The build was green.

test('metro-config exports everything that requires it expects', () => {
  const metroConfig = require(
    path.join(__dirname, '..', 'packages/react-native-basalt/dist/metro-config.js'),
  );
  for (const name of [
    'withDesktopPlatforms',
    'withLinuxPlatform',
    'appIdFor',
    'APP_ID_PREFIX',
    'DESKTOP_PLATFORMS',
    'SELF_IMPORTING_SHIMS',
    'PLATFORM_OVERRIDES',
    'MISSING_MODULES',
  ]) {
    assert.notEqual(metroConfig[name], undefined, `metro-config no longer exports ${name}`);
  }
});

test('react-native.config.js loads with nothing but Node', () => {
  // React Native's CLI reads this from the package root by path convention
  // rather than through `exports`, so it has to resolve without a bundler,
  // without a transpiler, and without this repository's own layout.
  const config = require(
    path.join(__dirname, '..', 'packages/react-native-basalt/react-native.config.js'),
  );
  assert.deepEqual(Object.keys(config.platforms).sort(), ['linux', 'macos', 'windows']);
  for (const platform of Object.values(config.platforms)) {
    assert.equal(typeof platform.projectConfig, 'function');
    assert.equal(platform.dependencyConfig(), null);
  }
});
