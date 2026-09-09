/**
 * The demo app, in React.
 *
 * Ordinary React Native throughout: hooks, StyleSheet, flexbox, Text, View,
 * Image, Pressable and ScrollView. Nothing here knows it is running on GTK4.
 */

'use strict';

import React, {useRef, useState} from 'react';
import {
  AppRegistry,
  Image,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';

const PALETTE = ['#4285f4', '#9b59f6', '#f26f56', '#56c98a', '#f2c14e'];

const IMAGE = {uri: 'assets/checker.png'};

const ROWS = Array.from({length: 24}, (_, index) => index);

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
  const [offsetY, setOffsetY] = useState(0);
  const scroller = useRef(null);

  return (
    <View style={styles.root}>
      <Text style={styles.heading}>React Native on GTK4</Text>

      <View style={styles.statusRow}>
        <Text style={styles.status}>contentOffset.y</Text>
        <Text style={styles.statusValue}>{String(Math.round(offsetY))}</Text>
      </View>

      <ScrollView
        ref={scroller}
        style={styles.scroller}
        contentContainerStyle={styles.scrollerContent}
        scrollEventThrottle={16}
        onScroll={event => setOffsetY(event.nativeEvent.contentOffset.y)}>
        {ROWS.map(index => (
          <View
            key={index}
            style={[styles.row, {backgroundColor: PALETTE[index % PALETTE.length]}]}>
            <Image source={IMAGE} resizeMode="cover" style={styles.thumb} />
            <Text style={styles.rowLabel}>row {index}</Text>
          </View>
        ))}
      </ScrollView>

      <View style={styles.controls}>
        <Button
          label="scroll to top"
          color={PALETTE[0]}
          onPress={() => scroller.current?.scrollTo({y: 0, animated: false})}
        />
        <Button
          label="scroll to end"
          color={PALETTE[3]}
          onPress={() => scroller.current?.scrollToEnd({animated: false})}
        />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, padding: 24, backgroundColor: '#11131a'},
  heading: {fontSize: 28, fontWeight: '700', color: '#f7f8fa', marginBottom: 10},
  statusRow: {flexDirection: 'row', alignItems: 'center', marginBottom: 14},
  status: {fontSize: 15, color: '#7f8794', marginRight: 10},
  statusValue: {fontSize: 22, fontWeight: '700', color: '#f7f8fa'},
  scroller: {flex: 1, backgroundColor: '#1a1e28', marginBottom: 16},
  scrollerContent: {padding: 12},
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    height: 72,
    marginBottom: 10,
    paddingHorizontal: 12,
  },
  thumb: {width: 72, height: 48, marginRight: 16},
  rowLabel: {fontSize: 20, fontWeight: '600', color: '#11131a'},
  controls: {flexDirection: 'row'},
  button: {
    flex: 1,
    height: 52,
    marginRight: 12,
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonLabel: {fontSize: 17, fontWeight: '600', color: '#11131a'},
});

AppRegistry.registerComponent('RNLinuxDemo', () => App);
