/**
 * react-native-basalt
 *
 * What this platform adds to React Native, and nothing else. Components come
 * from `react-native` exactly as they do on iOS and Android; this is where the
 * few things with no React Native equivalent live.
 *
 * It used to re-export all of `react-native` as well, and that is worth saying
 * why it no longer does, because the failure was quiet until something imported
 * this file. Babel compiles `export * from 'react-native'` into a loop that
 * reads every one of React Native's exports once -- and most of them are lazy
 * getters, which run their module's top level when read. So importing this
 * package loaded every deprecated component (and printed every deprecation
 * warning), and loaded DevMenu, whose getEnforcing throws on a host with no
 * DevMenu module. That throw aborted the importing module, which in an Expo app
 * is App.js, and the app stopped at "App entry not found". Nothing had imported
 * this file before the title bar gave apps a reason to.
 *
 * @format
 */

'use strict';

// Extensionless on purpose: Metro picks Platform.linux.js, Platform.macos.js or
// Platform.windows.js by the platform being bundled for, which is the same
// mechanism React Native uses for its own and means this line never has to know
// how many desktops there are.
export {default as Platform} from './overrides/Platform';
// The native file dialogs, which React Native has no API for: a phone has none.
// See useDialog.js.
export {useDialog, dialog} from './useDialog';
// The window's title bar: its title and colours, and a hidden style that lets
// the app draw its own header. See TitleBar.js.
export {TitleBar, useTitleBar, useTitleBarMetrics} from './TitleBar';
