/**
 * The demo app, in React.
 *
 * Everything here is ordinary React Native: hooks, StyleSheet, flexbox, and
 * AppRegistry. Nothing knows it is running on GTK4. The host starts a surface
 * with the module name registered at the bottom of this file, which is what
 * makes React reconcile into it.
 *
 * Only <View> has a GTK peer today, so this renders with views and colour
 * alone. <Text> needs a Pango TextLayoutManager, which is phase 4. Proving
 * reconciliation does not need text: the state changes below re-render on a
 * timer, and each re-render reaches the screen as Fabric Update mutations.
 */

'use strict';

import React, {useEffect, useMemo, useState} from 'react';
import {AppRegistry, StyleSheet, View} from 'react-native';

const PALETTE = ['#4285f4', '#9b59f6', '#f26f56', '#56c98a', '#f2c14e'];

function Bar({color, flex}) {
  return <View style={[styles.bar, {backgroundColor: color, flex}]} />;
}

function App() {
  const [tick, setTick] = useState(0);

  useEffect(() => {
    // setInterval also exercises the timer path: RN's JS timers run on
    // TimerManager, driven by PlatformTimerRegistryImpl on its own thread.
    const id = setInterval(() => setTick(current => current + 1), 1000);
    return () => clearInterval(id);
  }, []);

  // A rotating palette, so every tick is a props change on views that keep
  // their identity -- an Update rather than a teardown and rebuild.
  const colors = useMemo(
    () => PALETTE.map((_, index) => PALETTE[(index + tick) % PALETTE.length]),
    [tick],
  );

  // The row grows and shrinks by one child, so mounts and unmounts are
  // exercised too, not only updates.
  const barCount = 3 + (tick % 3);

  return (
    <View style={styles.root}>
      <View style={styles.row}>
        {colors.slice(0, barCount).map((color, index) => (
          <Bar key={index} color={color} flex={index === 0 ? 2 : 1} />
        ))}
      </View>

      <View style={[styles.footer, {backgroundColor: colors[4]}]}>
        <View style={[styles.badge, {backgroundColor: colors[1]}]} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    padding: 24,
    backgroundColor: '#11131a',
  },
  row: {
    flex: 1,
    flexDirection: 'row',
    marginBottom: 16,
  },
  bar: {
    marginRight: 12,
    borderRadius: 0,
  },
  footer: {
    height: 160,
    justifyContent: 'center',
    alignItems: 'flex-start',
  },
  badge: {
    width: 96,
    height: 96,
    marginLeft: 32,
  },
});

// The host passes this name to ReactHost::startSurface. A non-empty module
// name is what makes SurfaceHandler::start call AppRegistry.runApplication,
// which is where React takes over.
AppRegistry.registerComponent('RNLinuxDemo', () => App);
