/**
 * A React Native app about the application menu.
 *
 * Two things worth watching, and only one of them is on screen.
 *
 * The visible one is the menu itself: a File menu with the app's own commands,
 * and an Edit menu built entirely out of roles.
 *
 * The invisible one is why the Edit menu is there. On macOS AppKit hands every
 * key equivalent to the main menu before the responder chain sees it, so
 * `role="copy"` is what makes Cmd-C reach the <TextInput> below -- and an app
 * that renders no <Menu> at all still gets an Edit menu, because it still has
 * text fields. Type in the field and copy: without the menu, nothing happens.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AppRegistry,
  Platform,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import {Menu} from 'react-native-basalt';

console.log(`Platform.OS is ${Platform.OS}`);
console.log(`menu supported: ${Menu.isSupported}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  label: {color: '#aab', fontSize: 13, height: 22},
  field: {
    height: 44,
    borderRadius: 8,
    paddingHorizontal: 12,
    backgroundColor: '#1b1f2a',
    color: '#fff',
    marginBottom: 12,
  },
  chosen: {color: '#dde', fontSize: 14, height: 22},
});

function App() {
  const [chosen, setChosen] = React.useState('nothing yet');

  return (
    <View style={styles.page}>
      <Menu>
        <Menu.Submenu label="File">
          <Menu.Item
            label="New"
            accelerator="CmdOrCtrl+N"
            onClick={() => {
              console.log('menu chose: New');
              setChosen('New');
            }}
          />
          <Menu.Item
            label="Open…"
            accelerator="CmdOrCtrl+O"
            onClick={() => {
              console.log('menu chose: Open');
              setChosen('Open');
            }}
          />
          <Menu.Separator />
          <Menu.Item label="Nothing doing" enabled={false} onClick={() => {}} />
          <Menu.Item role="close" />
        </Menu.Submenu>
        <Menu.Submenu label="Edit">
          <Menu.Item role="undo" />
          <Menu.Item role="redo" />
          <Menu.Separator />
          <Menu.Item role="cut" />
          <Menu.Item role="copy" />
          <Menu.Item role="paste" />
          <Menu.Item role="selectAll" />
        </Menu.Submenu>
      </Menu>

      <Text style={styles.label}>Select this text and copy it</Text>
      <TextInput style={styles.field} defaultValue="Copy me with the Edit menu" />
      <Text style={styles.chosen}>{`chose: ${chosen}`}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltMenu', () => App);
