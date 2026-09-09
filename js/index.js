/**
 * The demo app, in React.
 *
 * Everything here is ordinary React Native: hooks, StyleSheet, flexbox, Text,
 * View and Pressable. Nothing knows it is running on GTK4.
 *
 * The counter is driven by presses, not a timer, so what is on screen is
 * evidence that a GTK click reached React's responder system and came back as
 * a re-render.
 */

'use strict';

import React, {useState} from 'react';
import {AppRegistry, Pressable, StyleSheet, Text, View} from 'react-native';

const PALETTE = ['#4285f4', '#9b59f6', '#f26f56', '#56c98a', '#f2c14e'];

function Button({label, onPress, color}) {
  return (
    <Pressable
      onPress={onPress}
      style={({pressed}) => [
        styles.button,
        {backgroundColor: color, opacity: pressed ? 0.55 : 1},
      ]}>
      <Text style={styles.buttonLabel}>{label}</Text>
    </Pressable>
  );
}

function App() {
  const [count, setCount] = useState(0);
  const [log, setLog] = useState('nothing pressed yet');

  const press = label => () => {
    setCount(current => (label === 'reset' ? 0 : current + (label === '+1' ? 1 : -1)));
    setLog(`last press: ${label}`);
  };

  return (
    <View style={styles.root}>
      <Text style={styles.heading}>React Native on GTK4</Text>

      <Text style={styles.body}>
        Pango measures this text, Yoga lays it out, and the buttons below run
        through React Native's responder system. A press changes the count,
        which is a real re-render rather than anything the widget layer did on
        its own.
      </Text>

      <View style={styles.counterRow}>
        <View style={[styles.counter, {backgroundColor: PALETTE[count % PALETTE.length]}]}>
          <Text style={styles.counterText}>{String(count)}</Text>
        </View>
        <Text style={styles.log}>{log}</Text>
      </View>

      <View style={styles.row}>
        <Button label="-1" color={PALETTE[2]} onPress={press('-1')} />
        <Button label="reset" color={PALETTE[1]} onPress={press('reset')} />
        <Button label="+1" color={PALETTE[3]} onPress={press('+1')} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    padding: 28,
    backgroundColor: '#11131a',
  },
  heading: {
    fontSize: 30,
    fontWeight: '700',
    color: '#f7f8fa',
    marginBottom: 14,
  },
  body: {
    fontSize: 16,
    lineHeight: 24,
    color: '#c3c9d5',
    marginBottom: 22,
  },
  counterRow: {
    flexDirection: 'row',
    alignItems: 'center',
    marginBottom: 22,
  },
  counter: {
    width: 96,
    height: 96,
    alignItems: 'center',
    justifyContent: 'center',
    marginRight: 18,
  },
  counterText: {
    fontSize: 44,
    fontWeight: '700',
    color: '#11131a',
  },
  log: {
    fontSize: 16,
    color: '#7f8794',
  },
  row: {
    flexDirection: 'row',
  },
  button: {
    flex: 1,
    height: 64,
    marginRight: 12,
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonLabel: {
    fontSize: 20,
    fontWeight: '600',
    color: '#11131a',
  },
});

AppRegistry.registerComponent('RNLinuxDemo', () => App);
