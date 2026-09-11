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
// Extensionless on purpose: Metro picks Platform.linux.js, Platform.macos.js or
// Platform.windows.js by the platform being bundled for, which is the same
// mechanism React Native uses for its own and means this line never has to know
// how many desktops there are.
export {default as Platform} from './overrides/Platform';
