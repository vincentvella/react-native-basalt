/**
 * A React Native app built around `<Image>`.
 *
 * Four resize modes over one small image, so that what `cover`, `contain`,
 * `stretch` and `center` mean is a picture rather than an argument -- they are
 * the part two platforms are most likely to disagree about while both looking
 * plausible on their own.
 *
 * Also one `data:` URI and one deliberately broken source, because the failure
 * path has to be a reported error rather than a blank box: `onError` firing is
 * the difference between "this image is missing" and "images are broken".
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Image, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

// A 4x2 PNG: two red pixels, two blue, twice. Small enough to read in a
// snapshot at any scale, and inline so this file needs no asset pipeline.
const INLINE =
  'data:image/png;base64,' +
  'iVBORw0KGgoAAAANSUhEUgAAAAQAAAACCAIAAADwyuo0AAAAFUlEQVR4nGN44OAA' +
  'RA4JD4CIAZkDAJQaC4Enje7+AAAAAElFTkSuQmCC';

const FILE = {uri: 'assets/checker.png'};
const MISSING = {uri: 'assets/there-is-no-such-file.png'};

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24},
  row: {flexDirection: 'row'},
  cell: {
    width: 180,
    height: 120,
    marginRight: 16,
    marginBottom: 16,
    backgroundColor: '#2b3445',
  },
  wide: {width: 376, height: 80, marginBottom: 16, backgroundColor: '#2b3445'},
});

function App() {
  return (
    <View style={styles.page}>
      <View style={styles.row}>
        <Image source={FILE} resizeMode="cover" style={styles.cell} />
        <Image source={FILE} resizeMode="contain" style={styles.cell} />
      </View>
      <View style={styles.row}>
        <Image source={FILE} resizeMode="stretch" style={styles.cell} />
        <Image source={FILE} resizeMode="center" style={styles.cell} />
      </View>
      <Image source={{uri: INLINE}} resizeMode="stretch" style={styles.wide} />
      <Image
        source={MISSING}
        style={styles.cell}
        onError={event => {
          console.log(`image error: ${event.nativeEvent.error}`);
        }}
        onLoad={() => {
          console.log('image error: none, which is the bug');
        }}
      />
    </View>
  );
}

AppRegistry.registerComponent('BasaltImage', () => App);
