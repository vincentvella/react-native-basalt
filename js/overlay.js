/**
 * A React Native app about React DevTools' element highlighter.
 *
 * `DebuggingOverlay` is the view DevTools mounts over an app to point at
 * something: the blue box over an inspected element, and the outlines around
 * everything that just re-rendered when "Highlight updates" is on. It is driven
 * entirely by commands rather than by props, which is why this app issues them
 * directly -- DevTools is the only other thing that would, and it needs a
 * DevTools session to be attached.
 *
 * Both kinds are here because they are drawn differently on purpose: an
 * inspected element is filled, a trace update is only outlined, and telling
 * them apart at a glance is the whole point.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';
import DebuggingOverlayNativeComponent, {
  Commands,
} from 'react-native/src/private/components/debuggingoverlay/specs/DebuggingOverlayNativeComponent';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  label: {color: '#aab', fontSize: 13, height: 22},
  card: {
    width: 200,
    height: 120,
    borderRadius: 10,
    backgroundColor: '#2b3140',
    marginBottom: 12,
  },
  overlay: {...StyleSheet.absoluteFill},
});

function App() {
  const overlay = React.useRef(null);

  React.useEffect(() => {
    if (overlay.current == null) {
      return;
    }
    // An inspected element: filled, and it stays until it is cleared.
    Commands.highlightElements(overlay.current, [
      {x: 24, y: 46, width: 200, height: 120},
    ]);
    console.log('overlay: highlighted an element');

    // A trace update: outlined, in the colour DevTools chose, and it clears
    // itself after a moment because the point is that it flashes.
    const timer = setTimeout(() => {
      if (overlay.current == null) {
        return;
      }
      Commands.highlightTraceUpdates(overlay.current, [
        {
          id: 1,
          rectangle: {x: 24, y: 180, width: 200, height: 120},
          color: 0xff00c853,
        },
      ]);
      console.log('overlay: highlighted a trace update');
    }, 1500);
    return () => clearTimeout(timer);
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>DebuggingOverlay</Text>
      <View style={styles.card} />
      <View style={styles.card} />
      <DebuggingOverlayNativeComponent ref={overlay} style={styles.overlay} />
    </View>
  );
}

AppRegistry.registerComponent('BasaltOverlay', () => App);
