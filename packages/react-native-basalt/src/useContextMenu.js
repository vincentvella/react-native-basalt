/**
 * A native context menu.
 *
 *   const menu = useContextMenu();
 *
 *   <Pressable
 *     onLongPress={event =>
 *       menu.show(
 *         [
 *           {label: 'Copy', onSelect: copy},
 *           {separator: true},
 *           {label: 'Delete', onSelect: remove, shortcut: 'Del'},
 *           {label: 'Paste', enabled: false},
 *         ],
 *         event.nativeEvent,
 *       )
 *     }
 *   />
 *
 * React Native has no API for this because a phone has no pointer to click the
 * other button of. On a desktop it is the second thing an app wants after a
 * menu bar -- and on Linux it is the *first*, because GNOME has no menu bar and
 * `Menu.isSupported` is false there.
 *
 * ## One level, on purpose
 *
 * No submenus and no roles. A popup menu on all three desktops is a list, and
 * the nesting an application menu has is what a menu *bar* is for -- so a
 * `<Menu>` and this are different shapes rather than the same shape twice.
 *
 * ## Where it appears
 *
 * `show(items, where)` takes anything with an `x` and a `y` in the window's
 * coordinates, which is what a press event's `nativeEvent` already is. Pass
 * nothing and the menu opens wherever the pointer is, which is what a menu
 * opened from the keyboard wants.
 *
 * ## What it answers
 *
 * `show()` resolves with the index chosen, or null if the menu was dismissed --
 * and calls that item's `onSelect` first, which is what most callers want:
 *
 *   const chosen = await menu.show(items);
 *
 * A promise rather than the device event `<Menu>` uses, because the two are
 * different situations. A menu bar is installed once and its items are chosen
 * at times nothing is waiting for; a context menu is opened by an app that is,
 * right then, asking a question.
 *
 * ## A right-click
 *
 *   <View
 *     onPointerDown={e => e.nativeEvent.button === 2 && menu.show(items, e.nativeEvent)}
 *   />
 *
 * A secondary click arrives as `onPointerDown` with W3C's `button === 2`, and
 * does *not* fire `onPress` -- on any of the three. So the same view can be a
 * button and have a context menu, which is what a desktop expects.
 *
 * `onLongPress` still works and is what a touch-first app should use. See
 * native/core/PointerButtons.h for what each toolkit had to be told.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {TurboModuleRegistry} from 'react-native';

const NativeMenu = TurboModuleRegistry.get('BasaltMenu');

/**
 * Whether this platform can show one.
 *
 * True on all three, unlike `Menu.isSupported`: a menu bar is what GNOME does
 * not have, and a popup menu is something every desktop has always had.
 */
export const isSupported = NativeMenu?.showContextMenu != null;

async function show(items, where) {
  if (!isSupported || !Array.isArray(items) || items.length === 0) {
    return null;
  }
  // Negative is "wherever the pointer is"; see the header. `pageX`/`pageY` as
  // well as `x`/`y`, because a press event carries the first pair and a caller
  // with a point in hand carries the second.
  const x = where?.x ?? where?.pageX ?? -1;
  const y = where?.y ?? where?.pageY ?? -1;

  const index = await NativeMenu.showContextMenu(items, x, y);
  if (index == null) {
    return null;
  }
  // Called before the promise settles, so that an app which only wants
  // `onSelect` never has to await anything.
  items[index]?.onSelect?.();
  return index;
}

/**
 * The imperative half, usable outside a component -- the same arrangement
 * `useDialog()` and `windowControl` have.
 */
export const contextMenu = Object.freeze({show, isSupported});

export function useContextMenu() {
  // Frozen and module-level, so this is a stable identity rather than a new
  // object every render: an app putting `menu` in a dependency array should not
  // get a new one each time.
  return contextMenu;
}
