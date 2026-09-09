/**
 * react-native-linux
 *
 * Everything React Native exports, plus what this platform adds. Today it adds
 * nothing, so this is a re-export -- but it is the seam that components with
 * their own component names will arrive through, starting with `<TextInput>`,
 * which cannot use React Native's own because both built-in implementations are
 * unreachable from here. See plan/decisions.md.
 *
 * @format
 */

'use strict';

export * from 'react-native';
export {default as Platform} from './overrides/Platform.linux';
