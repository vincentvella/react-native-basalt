/**
 * The application menu.
 *
 *   <Menu>
 *     <Menu.Submenu label="File">
 *       <Menu.Item label="Open…" accelerator="CmdOrCtrl+O" onClick={open} />
 *       <Menu.Separator />
 *       <Menu.Item role="close" />
 *     </Menu.Submenu>
 *     <Menu.Submenu label="Edit">
 *       <Menu.Item role="undo" />
 *       <Menu.Item role="copy" />
 *       <Menu.Item role="paste" />
 *     </Menu.Submenu>
 *   </Menu>
 *
 * Declarative because a menu bar is state, not a command: it is on screen for
 * as long as the component is mounted, and unmounting restores whatever the
 * platform's default was. `<Menu>` renders nothing.
 *
 * ## Roles
 *
 * An item with a `role` is one the platform implements itself, and it needs no
 * `onClick` and no label -- it gets the platform's own word and the platform's
 * own shortcut. The names are Electron's: about, quit, undo, redo, cut, copy,
 * paste, delete, selectAll, minimize, zoom, close, togglefullscreen.
 *
 * Roles are not a convenience. On macOS AppKit routes every key equivalent
 * through the main menu before anything else sees it, so `role="copy"` is what
 * makes Cmd-C reach a text field at all -- and an app that never renders a
 * <Menu> gets an Edit menu anyway, for exactly that reason.
 *
 * ## Where there is no menu bar
 *
 * `Menu.isSupported` is false on Linux, and that is not a gap waiting to be
 * filled: GNOME's guidelines have said to use a header bar with a menu button
 * since GNOME 3, and GTK4 removed the menu bar widget. An app that cares can
 * ask and put its commands in its own header with <TitleBar>. An app that does
 * not is no worse off -- the description is simply not shown.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

const NativeMenu = TurboModuleRegistry.get('BasaltMenu');

// Must match kMenuChosenEvent in native/core/MenuModule.h.
const CHOSEN_EVENT = 'basaltMenuItemChosen';

/** Whether this platform shows an application menu at all. See the header. */
export const isSupported = NativeMenu?.isSupported?.() === true;

// Every `onClick` in the tree, by the id the native side reports back. Ids are
// assigned here rather than by the app: an app numbering its own items would be
// an app with two items numbered 3.
let nextId = 1;

function describe(children, handlers) {
  const items = [];
  React.Children.forEach(children, child => {
    if (child == null || typeof child !== 'object') {
      return;
    }
    const {kind, label, role, accelerator, enabled, onClick} = child.props ?? {};

    if (child.type === Separator) {
      items.push({separator: true});
      return;
    }
    if (child.type === Submenu) {
      items.push({
        label: label ?? '',
        enabled: enabled !== false,
        submenu: describe(child.props.children, handlers),
      });
      return;
    }
    if (child.type !== Item) {
      // Anything else in a <Menu> is a mistake rather than a thing to render:
      // there is nowhere for it to go.
      return;
    }

    const item = {
      label: label ?? '',
      role: role ?? '',
      accelerator: accelerator ?? '',
      enabled: enabled !== false,
      id: 0,
    };
    // A role is handled by the platform and never comes back, so it needs no
    // id and no handler. See the header.
    if (!role && typeof onClick === 'function') {
      item.id = nextId++;
      handlers.set(item.id, onClick);
    }
    items.push(item);
    void kind;
  });
  return items;
}

function Item() {
  return null;
}
function Separator() {
  return null;
}
function Submenu() {
  return null;
}

export function Menu({children}) {
  const handlers = React.useRef(new Map());

  // Described during render rather than in the effect, so that the effect's
  // dependency is the menu itself: a menu whose labels did not change should
  // not be reinstalled, and reinstalling one closes it if it happens to be open.
  const items = React.useMemo(() => {
    handlers.current = new Map();
    return describe(children, handlers.current);
  }, [children]);
  const serialised = React.useMemo(() => JSON.stringify(items), [items]);

  React.useEffect(() => {
    NativeMenu?.setApplicationMenu?.(JSON.parse(serialised));
    return () => {
      // Back to the platform's default, which on macOS is the minimum an
      // application needs to be usable -- an Edit menu included.
      NativeMenu?.setApplicationMenu?.([]);
    };
  }, [serialised]);

  React.useEffect(() => {
    const subscription = DeviceEventEmitter.addListener(CHOSEN_EVENT, id => {
      const onClick = handlers.current.get(id);
      if (onClick != null) {
        onClick();
      }
    });
    return () => subscription.remove();
  }, []);

  return null;
}

Menu.Item = Item;
Menu.Separator = Separator;
Menu.Submenu = Submenu;
Menu.isSupported = isSupported;
