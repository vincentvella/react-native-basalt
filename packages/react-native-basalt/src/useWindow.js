/**
 * The window: how big it is, where it is, and what it is doing.
 *
 *   const window = useWindow();
 *
 *   window.setSize(900, 700);
 *   window.setPosition(100, 100);
 *   window.center();
 *   window.setFullScreen(true);
 *   window.minimize();
 *   window.toggleMaximize();
 *   window.close();
 *
 *   const {width, height, x, y, fullScreen, maximized} = window.bounds;
 *
 * React Native has no API for any of this: on a phone there is one window, it
 * is the screen, and nothing an app says would change it. On a desktop it is
 * the first thing an app wants after it has drawn something.
 *
 * ## `bounds` is live
 *
 * It re-renders when the window moves, is resized, or changes state, because
 * the alternative -- a `getBounds()` an app calls and caches -- is an app whose
 * layout is right until somebody drags a corner. The host emits the change; see
 * native/core/WindowMethods.h.
 *
 * Reading it before there is a window gives zeroes rather than null, so that
 * `const {width} = window.bounds` never throws during a first render.
 *
 * ## What the platforms will not do
 *
 * `setPosition` and `center` do nothing on Linux, and `bounds.x` and `bounds.y`
 * are always zero there. This is not unimplemented: GTK4 removed
 * `gtk_window_move` and Wayland has no equivalent, because a client has no idea
 * where its window is and no say in it. Both display servers centre an unplaced
 * window by default, so an app that never moves its window already gets what
 * `center()` would have asked for.
 *
 * Every other platform difference is spelling. `setSize` is a request on all
 * three -- a tiling window manager may ignore it -- which is why `bounds`
 * reports what happened rather than what was asked for.
 *
 * ## Refusing to close
 *
 *   useCloseRequest(close => {
 *     if (unsaved) {
 *       setAsking(true);
 *     } else {
 *       close();
 *     }
 *   });
 *
 * A window with a close handler stops closing on its own: every attempt -- its
 * close button, Cmd-W, Alt+F4, the window manager -- is refused and reported,
 * and the window goes when the app says so by calling the `close` it was
 * handed. Which is the point: there is nowhere else to put "are you sure".
 *
 * On macOS this covers closing the window and not Cmd-Q, which terminates the
 * application without asking any window whether it minds.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

import {mainWindowId, useCloseRequestFor} from './closeRequest';

const NativeWindow = TurboModuleRegistry.get('BasaltWindow');

// Must match kWindowBoundsEvent in native/core/WindowMethods.h.
const BOUNDS_EVENT = 'basaltWindowBoundsChanged';

const NO_BOUNDS = Object.freeze({
  x: 0,
  y: 0,
  width: 0,
  height: 0,
  fullScreen: false,
  maximized: false,
});

function readBounds() {
  if (NativeWindow?.getBounds == null) {
    return NO_BOUNDS;
  }
  const bounds = NativeWindow.getBounds();
  return bounds != null ? bounds : NO_BOUNDS;
}

/**
 * The imperative half, usable outside a component.
 *
 * Every call is a no-op on a host with no window module, so the same code runs
 * on a platform that has not implemented one.
 */
export const windowControl = Object.freeze({
  setSize(width, height) {
    NativeWindow?.setSize?.(width, height);
  },
  setPosition(x, y) {
    NativeWindow?.setPosition?.(x, y);
  },
  center() {
    NativeWindow?.center?.();
  },
  setFullScreen(fullScreen) {
    NativeWindow?.setFullScreen?.(fullScreen === true);
  },
  minimize() {
    NativeWindow?.minimize?.();
  },
  toggleMaximize() {
    NativeWindow?.toggleMaximize?.();
  },
  close() {
    NativeWindow?.close?.();
  },
  getBounds() {
    return readBounds();
  },
});

/**
 * Be asked before this window closes.
 *
 * Pass null or nothing to stop being asked, which is also what unmounting does.
 * See the header above, and `<Window onCloseRequest>` for a second window.
 */
export function useCloseRequest(handler) {
  useCloseRequestFor(mainWindowId, () => windowControl.close(), handler, []);
}

export function useWindow() {
  const [bounds, setBounds] = React.useState(readBounds);

  React.useEffect(() => {
    // Read again on mount as well as subscribing. The window may have been
    // resized between the first render and this effect -- which on a host that
    // opens its window and mounts into it is the common case, not the rare one.
    setBounds(readBounds());

    const subscription = DeviceEventEmitter.addListener(BOUNDS_EVENT, next => {
      setBounds(next ?? NO_BOUNDS);
    });
    return () => subscription.remove();
  }, []);

  return React.useMemo(() => ({...windowControl, bounds}), [bounds]);
}
