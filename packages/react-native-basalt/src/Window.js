/**
 * A second window.
 *
 *   <Window title="Preview" width={600} height={400} onClose={hide}>
 *     <Preview id={id} />
 *   </Window>
 *
 * Mount it and a window opens; unmount it and the window closes. React Native
 * has no API for this because a phone has one window and it is the screen.
 *
 * ## A window is a React root
 *
 * That is Fabric's grain rather than a decision this makes: a surface is what
 * has a size, a layout context and a root shadow node, and two windows sharing
 * one would be two windows sharing a layout. So a second window runs a second
 * React tree, and `<Window>`'s children are rendered *there* rather than where
 * they were written.
 *
 * Which has a consequence worth stating plainly, because it is the one thing
 * that will surprise people: **the children do not see React context from the
 * tree they were written in.** A theme provider, a navigation container or a
 * Redux store wrapping the `<Window>` is not wrapping its children. What does
 * cross is props -- the elements themselves are passed through, so anything
 * captured in a closure or given as a prop arrives intact, including state and
 * callbacks:
 *
 *   const [count, setCount] = useState(0);
 *   <Window><Text onPress={() => setCount(count + 1)}>{count}</Text></Window>
 *
 * works, and re-renders the window when `count` changes. A `useContext` inside
 * `<Preview>` does not. An app that needs context in a window should pass the
 * value as a prop and re-provide it inside.
 *
 * `useWindow()` inside a second window's tree still reports the *active*
 * window, not the one it is in; that is a real gap and is in plan/backlog.md.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, TurboModuleRegistry} from 'react-native';

const NativeWindows = TurboModuleRegistry.get('BasaltWindows');

/** Whether this platform can open a second window at all. */
export const isSupported = NativeWindows != null;

// What each open window is rendering, by the id `<Window>` gave it, and who to
// tell when it changes. Module level because the two ends are in different
// React roots and have no ancestor in common -- which is the whole situation.
const contents = new Map();
const listeners = new Map();

let nextId = 1;

function setContent(id, children) {
  contents.set(id, children);
  const listener = listeners.get(id);
  if (listener != null) {
    listener(children);
  }
}

/**
 * The component every second window runs.
 *
 * Registered once, under a name an app will not collide with. It renders
 * whatever its `<Window>` currently has as children, and subscribes so that a
 * re-render of the first tree reaches the second.
 */
function WindowContent({basaltWindowId}) {
  const [children, setChildren] = React.useState(() => contents.get(basaltWindowId) ?? null);

  React.useEffect(() => {
    listeners.set(basaltWindowId, setChildren);
    // Read again on mount: the first tree may have re-rendered between the
    // window opening and this root evaluating, and the map is the truth.
    setChildren(contents.get(basaltWindowId) ?? null);
    return () => {
      listeners.delete(basaltWindowId);
    };
  }, [basaltWindowId]);

  return children;
}

const CONTENT_COMPONENT = 'BasaltWindowContent';
let registered = false;

function ensureRegistered() {
  if (registered || !isSupported) {
    return;
  }
  registered = true;
  AppRegistry.registerComponent(CONTENT_COMPONENT, () => WindowContent);
}

export function Window({title, width, height, children, onClose}) {
  const id = React.useRef(null);
  if (id.current == null) {
    id.current = nextId++;
  }
  const windowId = id.current;

  // Written during render rather than in an effect, so that the second root
  // never renders a frame behind the first. Rendering is not supposed to have
  // effects; this one is a write to a map the other root reads, which is the
  // narrowest version of what a portal does.
  setContent(windowId, children);

  // The surface id the host gave us, which is what closes the window. Kept in a
  // ref rather than in state: nothing renders differently for it.
  const surfaceId = React.useRef(null);

  // Deliberately not depending on title, width or height. They are how a window
  // *opens*, and re-opening one because its title changed would be a new window
  // in a new place. Changing them afterwards is `useWindow()`'s job.
  React.useEffect(() => {
    if (!isSupported) {
      return;
    }
    ensureRegistered();

    let closed = false;
    NativeWindows.open({
      component: CONTENT_COMPONENT,
      title: title ?? '',
      width: width ?? 900,
      height: height ?? 700,
      props: {basaltWindowId: windowId},
    }).then(opened => {
      // Unmounted while the window was opening, which is a race a fast app
      // really does lose: close what was opened rather than leaking it.
      if (closed) {
        if (opened != null) {
          NativeWindows.close(opened);
        }
        return;
      }
      surfaceId.current = opened;
    });

    return () => {
      closed = true;
      if (surfaceId.current != null) {
        NativeWindows.close(surfaceId.current);
        surfaceId.current = null;
      }
      contents.delete(windowId);
      listeners.delete(windowId);
      if (onClose != null) {
        onClose();
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [windowId]);

  return null;
}

Window.isSupported = isSupported;
