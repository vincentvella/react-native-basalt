/**
 * The demo app, in React.
 *
 * Ordinary React Native throughout: hooks, StyleSheet, flexbox, Text, View,
 * Image and Pressable. Nothing here knows it is running on GTK4.
 */

'use strict';

import React, {useState} from 'react';
import {AppRegistry, Image, Pressable, StyleSheet, Text, View} from 'react-native';

const PALETTE = ['#4285f4', '#9b59f6', '#f26f56', '#56c98a', '#f2c14e'];

// A file path rather than a require(): asset registration is bundler work that
// belongs with the npm package, not the renderer. The loader also takes
// http(s) and data: URIs.
const IMAGE = {uri: 'assets/checker.png'};

const FITS = ['cover', 'contain', 'stretch', 'center'];

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
  const [fitIndex, setFitIndex] = useState(0);
  const fit = FITS[fitIndex];

  return (
    <View style={styles.root}>
      <Text style={styles.heading}>React Native on GTK4</Text>

      <Text style={styles.body}>
        The image below is loaded and decoded off the main thread, then painted
        as a GdkTexture. Press a button to change its resizeMode; the frame
        stays the same size, so what moves is how the pixels fill it.
      </Text>

      <View style={styles.imageRow}>
        <View style={styles.imageFrame}>
          <Image source={IMAGE} resizeMode={fit} style={styles.image} />
        </View>
        <View style={styles.legend}>
          <Text style={styles.legendLabel}>resizeMode</Text>
          <Text style={styles.legendValue}>{fit}</Text>
        </View>
      </View>

      <View style={styles.row}>
        {FITS.map((name, index) => (
          <Button
            key={name}
            label={name}
            color={PALETTE[index]}
            onPress={() => setFitIndex(index)}
          />
        ))}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 28, backgroundColor: '#11131a'},
  heading: {fontSize: 30, fontWeight: '700', color: '#f7f8fa', marginBottom: 14},
  body: {fontSize: 16, lineHeight: 24, color: '#c3c9d5', marginBottom: 22},
  imageRow: {flexDirection: 'row', alignItems: 'flex-start', marginBottom: 22},
  imageFrame: {
    width: 300,
    height: 200,
    backgroundColor: '#1e222c',
    marginRight: 24,
  },
  image: {flex: 1},
  legend: {paddingTop: 8},
  legendLabel: {fontSize: 14, color: '#7f8794', marginBottom: 4},
  legendValue: {fontSize: 26, fontWeight: '700', color: '#f7f8fa'},
  row: {flexDirection: 'row'},
  button: {
    flex: 1,
    height: 56,
    marginRight: 12,
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonLabel: {fontSize: 17, fontWeight: '600', color: '#11131a'},
});

AppRegistry.registerComponent('RNLinuxDemo', () => App);
