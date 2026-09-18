/**
 * A React Native app built around `<ScrollView>`.
 *
 * A ScrollView is not one feature but four that have to agree: Yoga letting the
 * content exceed the viewport, the platform clipping it, the offset moving
 * children *and* being written back into ScrollViewState, and `onScroll`
 * reaching JavaScript. Three of those are invisible from a screenshot, so the
 * row index is rendered as a width -- each row is as wide as its number -- and
 * the scroll position is reported through onScroll and logged.
 *
 * It also scrolls itself, once, through `scrollTo` on a ref. That is the
 * imperative command path, which is a separate seam from the wheel -- it
 * arrives on the JS thread and has to be marshalled -- and it is the only way
 * to scroll both hosts identically from one file, since a wheel has to be
 * injected per platform.
 *
 * No <Text>, so the same file runs on a platform with no text engine, and no
 * VirtualizedList, so a failure here is this project's rather than React
 * Native's.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, ScrollView, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const ROWS = 24;

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24},
  scroller: {flex: 1, backgroundColor: '#2b3445', borderRadius: 8},
  row: {height: 64, marginHorizontal: 16, marginTop: 16, borderRadius: 6},
  marker: {height: 24, marginTop: 16, marginHorizontal: 16, backgroundColor: '#f27359'},
});

function App() {
  const scroller = React.useRef(null);

  React.useEffect(() => {
    // Late enough that the content has been measured and committed: scrollTo
    // clamps against the content size, and a command that arrives before the
    // first layout would clamp to zero and look like it did nothing.
    const timer = setTimeout(() => {
      scroller.current?.scrollTo({y: 530, animated: false});
    }, 800);
    return () => clearTimeout(timer);
  }, []);

  return (
    <View style={styles.page}>
      <ScrollView
        ref={scroller}
        style={styles.scroller}
        scrollEventThrottle={16}
        onScroll={event => {
          const {y} = event.nativeEvent.contentOffset;
          console.log(`scrolled to ${Math.round(y)}`);
        }}>
        {Array.from({length: ROWS}, (_, index) => (
          <View
            key={index}
            style={[
              styles.row,
              {
                // Width encodes the index, so which rows are on screen is
                // readable from a screenshot and from the tree dump.
                width: 80 + index * 24,
                backgroundColor: index % 2 === 0 ? '#4d8cf2' : '#59cc8c',
              },
            ]}
          />
        ))}
        <View style={styles.marker} />
      </ScrollView>
    </View>
  );
}

AppRegistry.registerComponent('BasaltScroll', () => App);

/**
 * The same list, scrolled with `animated: true`.
 *
 * Its own screen rather than a second button, because the two assert opposite
 * things: the app above must arrive at once, and this one must not. A screen
 * that did both would have two answers to "where is it now".
 *
 * What is worth watching is the offsets in between. Arriving at 530 proves
 * nothing -- an instant jump does that too -- so the demo logs every offset it
 * is told about, and the interesting assertion is that some of them are neither
 * 0 nor 530.
 */
function Animated() {
  const scroller = React.useRef(null);

  React.useEffect(() => {
    const timer = setTimeout(() => {
      console.log('scroll: animating to 530');
      scroller.current?.scrollTo({y: 530, animated: true});
    }, 800);
    return () => clearTimeout(timer);
  }, []);

  return (
    <View style={styles.page}>
      <ScrollView
        ref={scroller}
        style={styles.scroller}
        scrollEventThrottle={16}
        onScroll={event => {
          const {y} = event.nativeEvent.contentOffset;
          console.log(`scrolled to ${Math.round(y)}`);
        }}>
        {Array.from({length: ROWS}, (_, index) => (
          <View
            key={index}
            style={[
              styles.row,
              {
                width: 80 + index * 24,
                backgroundColor: index % 2 === 0 ? '#4d8cf2' : '#59cc8c',
              },
            ]}
          />
        ))}
        <View style={styles.marker} />
      </ScrollView>
    </View>
  );
}

AppRegistry.registerComponent('BasaltScrollAnimated', () => Animated);
