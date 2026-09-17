/**
 * A React Native app about the window it is in.
 *
 * React Native has no API for this: on a phone there is one window, it is the
 * screen, and nothing an app says would change it. So, like js/dialogs.js, this
 * is a screen about something React Native does not do at all.
 *
 * Its own bounds are on screen and logged, which is the half worth asserting:
 * `bounds` is live, so a window resized by anything -- the person, the window
 * manager, or the buttons here -- re-renders the app with the new numbers. An
 * app that cached `getBounds()` would be right until somebody dragged a corner.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, Text, View} from 'react-native';
import {useWindow} from 'react-native-basalt';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  // Fixed heights, so the buttons sit at the same y on all three hosts: text
  // measurement is the one thing the three text engines will never agree on.
  label: {color: '#aab', fontSize: 13, height: 22},
  row: {flexDirection: 'row', height: 48, marginBottom: 12},
  button: {
    width: 150,
    height: 48,
    marginRight: 12,
    borderRadius: 8,
    backgroundColor: '#4285f4',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#fff', fontSize: 15},
});

function App() {
  const window = useWindow();
  const {width, height, x, y, fullScreen, maximized} = window.bounds;

  // Logged on every change, which is what says the event arrived rather than
  // that the first read did.
  React.useEffect(() => {
    console.log(
      `window bounds: ${width}x${height} at ${x},${y} ` +
        `fullScreen=${fullScreen} maximized=${maximized}`,
    );
  }, [width, height, x, y, fullScreen, maximized]);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>Window</Text>
      <View style={styles.row}>
        <Pressable style={styles.button} onPress={() => window.setSize(700, 500)}>
          <Text style={styles.buttonText}>700 x 500</Text>
        </Pressable>
        <Pressable style={styles.button} onPress={() => window.setSize(1000, 800)}>
          <Text style={styles.buttonText}>1000 x 800</Text>
        </Pressable>
      </View>
      <View style={styles.row}>
        <Pressable style={styles.button} onPress={() => window.center()}>
          <Text style={styles.buttonText}>Centre</Text>
        </Pressable>
        <Pressable
          style={styles.button}
          onPress={() => window.setFullScreen(!fullScreen)}>
          <Text style={styles.buttonText}>
            {fullScreen ? 'Leave full screen' : 'Full screen'}
          </Text>
        </Pressable>
      </View>
      <Text style={styles.label}>
        {`${width} x ${height} at ${x}, ${y}`}
      </Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltWindow', () => App);
