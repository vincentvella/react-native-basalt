/**
 * A React Native app with more than one window.
 *
 * A second window is a second surface and so a second React tree, which is
 * Fabric's grain rather than a decision this made: a surface is what has a
 * size, a layout context and a root shadow node. So the interesting thing to
 * watch is not that a window opens -- it is that the two trees stay in step.
 *
 * The counter lives in the first window's state. The second window renders it,
 * and its button calls back into the first window's setter. Both windows show
 * the same number because there is only one, and it crosses as a prop and a
 * closure rather than through context, which is exactly the boundary
 * `<Window>`'s header describes.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, Text, View} from 'react-native';
import {Window} from 'react-native-basalt';

console.log(`Platform.OS is ${Platform.OS}`);
console.log(`windows supported: ${Window.isSupported}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  second: {flex: 1, backgroundColor: '#1b1f2a', padding: 24},
  label: {color: '#aab', fontSize: 13, height: 22},
  count: {color: '#fff', fontSize: 28, height: 40},
  button: {
    width: 220,
    height: 48,
    borderRadius: 8,
    marginBottom: 12,
    backgroundColor: '#4285f4',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#fff', fontSize: 15},
});

function App() {
  const [open, setOpen] = React.useState(false);
  const [count, setCount] = React.useState(0);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>Main window</Text>
      <Text style={styles.count}>{`count ${count}`}</Text>

      <Pressable
        style={styles.button}
        onPress={() => {
          console.log(`windows: ${open ? 'closing' : 'opening'} the second window`);
          setOpen(!open);
        }}>
        <Text style={styles.buttonText}>
          {open ? 'Close the second window' : 'Open a second window'}
        </Text>
      </Pressable>

      <Pressable style={styles.button} onPress={() => setCount(c => c + 1)}>
        <Text style={styles.buttonText}>Count up here</Text>
      </Pressable>

      {open ? (
        <Window
          title="The second window"
          width={520}
          height={360}
          onClose={() => {
            // The person closed it, not the app. Without this the flag below
            // would stay true and the window could never be reopened.
            console.log('windows: the second window closed itself');
            setOpen(false);
          }}>
          <View style={styles.second}>
            <Text style={styles.label}>Second window</Text>
            {/* The same number, from the same state, in a different tree. */}
            <Text style={styles.count}>{`count ${count}`}</Text>
            <Pressable
              style={styles.button}
              onPress={() => {
                console.log('windows: counted up from the second window');
                setCount(c => c + 1);
              }}>
              <Text style={styles.buttonText}>Count up over there</Text>
            </Pressable>
          </View>
        </Window>
      ) : null}
    </View>
  );
}

AppRegistry.registerComponent('BasaltWindows', () => App);
