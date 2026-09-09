/**
 * The demo app, in React.
 *
 * Everything here is ordinary React Native: hooks, StyleSheet, flexbox, Text
 * and View. Nothing knows it is running on GTK4. The host starts a surface with
 * the module name registered at the bottom of this file, which is what makes
 * React reconcile into it.
 *
 * The text on screen is measured by Pango through React Native's
 * TextLayoutManager seam, so Yoga sizes these paragraphs the same way it sizes
 * them on iOS and Android.
 */

'use strict';

import React, {useEffect, useMemo, useState} from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';

const PALETTE = ['#4285f4', '#9b59f6', '#f26f56', '#56c98a', '#f2c14e'];

const PARAGRAPH =
  'Yoga asked Pango how wide this paragraph wants to be, and Pango answered ' +
  'in 1024ths of a pixel. It wraps because the column is narrow, not because ' +
  'anything here counted characters.';

function Swatch({color, label}) {
  return (
    <View style={[styles.swatch, {backgroundColor: color}]}>
      <Text style={styles.swatchLabel}>{label}</Text>
    </View>
  );
}

function App() {
  const [tick, setTick] = useState(0);

  useEffect(() => {
    const id = setInterval(() => setTick(current => current + 1), 1000);
    return () => clearInterval(id);
  }, []);

  const colors = useMemo(
    () => PALETTE.map((_, index) => PALETTE[(index + tick) % PALETTE.length]),
    [tick],
  );

  return (
    <View style={styles.root}>
      <Text style={styles.heading}>React Native on GTK4</Text>

      <Text style={styles.body}>{PARAGRAPH}</Text>

      <Text style={styles.body}>
        Nested spans work too:{' '}
        <Text style={styles.bold}>bold</Text>,{' '}
        <Text style={styles.italic}>italic</Text>, and{' '}
        <Text style={{color: colors[2]}}>a colour that changes every second</Text>.
      </Text>

      <Text style={styles.clipped} numberOfLines={1}>
        This line is limited to one line and ellipsized at the tail, which is
        Pango truncating it rather than JavaScript slicing a string.
      </Text>

      <View style={styles.row}>
        {colors.slice(0, 4).map((color, index) => (
          <Swatch key={index} color={color} label={String(tick + index)} />
        ))}
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
    fontSize: 32,
    fontWeight: '700',
    color: '#f7f8fa',
    marginBottom: 16,
  },
  body: {
    fontSize: 16,
    lineHeight: 24,
    color: '#c3c9d5',
    marginBottom: 14,
  },
  bold: {
    fontWeight: '700',
    color: '#f7f8fa',
  },
  italic: {
    fontStyle: 'italic',
    color: '#f7f8fa',
  },
  clipped: {
    fontSize: 16,
    color: '#7f8794',
    marginBottom: 20,
  },
  row: {
    flex: 1,
    flexDirection: 'row',
  },
  swatch: {
    flex: 1,
    marginRight: 12,
    padding: 12,
  },
  swatchLabel: {
    fontSize: 20,
    fontWeight: '600',
    color: '#11131a',
  },
});

AppRegistry.registerComponent('RNLinuxDemo', () => App);
