/**
 * A React Native app whose whole job is to react to a press.
 *
 * `<Pressable>` runs on Pressability, which runs on the responder system in
 * JavaScript, which is fed by touchstart/touchmove/touchend. So this exercises
 * the entire input path and not only the part a platform writes: hit testing,
 * emitter lookup, the event beat that flushes the queue, the responder
 * negotiation, React re-rendering, and the mutation coming back down.
 *
 * The counter is rendered as a row of boxes rather than as text, so the same
 * file proves the same thing on a platform with no text engine. The count is
 * also logged, which is what `scripts/compare_hosts.sh` and the automated taps
 * assert on.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24},
  button: {
    height: 120,
    borderRadius: 12,
    backgroundColor: '#4d8cf2',
    alignItems: 'center',
    justifyContent: 'center',
  },
  pressed: {backgroundColor: '#f27359'},
  tally: {flexDirection: 'row', marginTop: 24},
  pip: {
    width: 40,
    height: 40,
    borderRadius: 20,
    marginRight: 12,
    backgroundColor: '#59cc8c',
  },
  inner: {width: 64, height: 64, borderRadius: 8, backgroundColor: '#e6eeff'},
});

function App() {
  const [count, setCount] = React.useState(0);

  return (
    <View style={styles.page}>
      <Pressable
        style={({pressed}) => [styles.button, pressed && styles.pressed]}
        onPress={() => {
          setCount(previous => {
            const next = previous + 1;
            console.log(`pressed ${next}`);
            return next;
          });
        }}>
        <View style={styles.inner} />
      </Pressable>
      <View style={styles.tally}>
        {Array.from({length: count}, (_, index) => (
          <View key={index} style={styles.pip} />
        ))}
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltPress', () => App);
