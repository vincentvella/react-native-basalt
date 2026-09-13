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

const {createTitleBarStack, sameRequest} = require(
  path.join(__dirname, '..', 'packages/react-native-basalt/src/titleBarState.js'),
);

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
