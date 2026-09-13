/**
 * The title bar requests currently mounted, and what they add up to.
 *
 * Kept apart from TitleBar.js, with no React Native in it, so that it can be
 * tested with nothing but Node -- this is the part with rules in it.
 *
 * Several screens may each ask for something, as they do of <StatusBar>, and
 * the most recently mounted request wins. But key by key: a screen that only
 * sets a title does not reset the colour an app shell underneath it set, and
 * when it unmounts the shell's title comes back.
 *
 * @format
 */

'use strict';

const TITLE_BAR_KEYS = ['title', 'style', 'backgroundColor', 'textColor', 'borderColor'];

function pick(options) {
  const picked = {};
  for (const key of TITLE_BAR_KEYS) {
    if (options != null && options[key] !== undefined) {
      picked[key] = options[key];
    }
  }
  return picked;
}

function createTitleBarStack() {
  const entries = [];
  let nextId = 1;

  return {
    push(options) {
      const id = nextId++;
      entries.push({id, options: pick(options)});
      return id;
    },

    update(id, options) {
      const entry = entries.find(candidate => candidate.id === id);
      if (entry != null) {
        entry.options = pick(options);
      }
    },

    remove(id) {
      const index = entries.findIndex(candidate => candidate.id === id);
      if (index !== -1) {
        entries.splice(index, 1);
      }
    },

    // Later entries override earlier ones, one key at a time.
    resolve() {
      const resolved = {};
      for (const {options} of entries) {
        Object.assign(resolved, options);
      }
      return resolved;
    },

    get size() {
      return entries.length;
    },
  };
}

// Whether two resolved requests would ask the host for the same thing, so that
// a re-render that changed nothing does not cross into native at all.
function sameRequest(a, b) {
  return TITLE_BAR_KEYS.every(key => (a ?? {})[key] === (b ?? {})[key]);
}

module.exports = {TITLE_BAR_KEYS, createTitleBarStack, sameRequest};
