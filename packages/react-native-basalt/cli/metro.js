/**
 * Starting Metro, or noticing that it is already running.
 *
 * Deliberately not a Metro API call. `run-android` and `run-ios` both shell out
 * to `react-native start` in a detached process for the same reason: the
 * packager outlives the run command, and an app is normally launched several
 * times against one Metro.
 *
 * @format
 */

'use strict';

const {spawn} = require('child_process');
const fs = require('fs');
const net = require('net');
const path = require('path');

/**
 * Whether something is already listening.
 *
 * Only that something is listening, not that it is Metro: a port in use by
 * anything else is a problem the developer has to see, and starting a second
 * Metro on top of it would hide it.
 */
function isPortTaken(port, host = 'localhost') {
  return new Promise(resolve => {
    const socket = net.connect({port, host});
    const done = taken => {
      socket.destroy();
      resolve(taken);
    };
    socket.setTimeout(1500);
    socket.once('connect', () => done(true));
    socket.once('timeout', () => done(false));
    socket.once('error', () => done(false));
  });
}

async function waitForPort(port, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await isPortTaken(port)) {
      return true;
    }
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  return false;
}

/**
 * Starts Metro and returns once it is listening.
 *
 * Detached and unref'd, so quitting the app does not kill the packager and the
 * next `run-linux` reuses it.
 *
 * Its output goes to a file rather than to this terminal, which looks like the
 * unfriendly choice and is not. A detached child that inherits stdout holds the
 * pipe open after this command exits, so anything reading `run-linux`'s output
 * waits forever for a packager that is meant to outlive it. This repository's
 * own CI hit the same shape and its workflow carries the same warning. The path
 * is printed, and `--no-packager` with a packager of your own is the answer if
 * you want it on screen.
 */
async function startMetro(context, port) {
  const cli = path.join(context.reactNativePath, 'cli.js');

  const logDir = path.join(context.root, '.basalt');
  fs.mkdirSync(logDir, {recursive: true});
  const logPath = path.join(logDir, 'metro.log');
  const log = fs.openSync(logPath, 'a');

  const child = spawn(process.execPath, [cli, 'start', '--port', String(port)], {
    cwd: context.root,
    detached: true,
    stdio: ['ignore', log, log],
  });
  child.unref();
  fs.closeSync(log);

  if (!(await waitForPort(port, 90000))) {
    throw new Error(
      `Metro did not start listening on port ${port}; see ${logPath}`,
    );
  }
  return {child, logPath};
}

module.exports = {isPortTaken, waitForPort, startMetro};
