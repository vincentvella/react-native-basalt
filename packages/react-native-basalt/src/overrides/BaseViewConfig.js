/**
 * React Native's own base view config, plus three props it forgot to declare.
 *
 * `onPointerDown`, `onPointerUp` and `onPointerCancel` are registered in
 * `bubblingEventTypes` -- `topPointerDown` and friends, with their bubbled and
 * captured names -- and are supported the whole way down: `propsConversions.h`
 * parses them into `ViewProps::events`, `PointerEventsProcessor` handles them,
 * and `TouchEventEmitter::onPointerDown` exists to dispatch one.
 *
 * What is missing is one line in `validAttributes`, which lists
 * `onPointerEnter`, `onPointerLeave`, `onPointerMove`, `onPointerOut` and
 * `onPointerOver` and stops there. Without it React never sends the prop, so
 * `ViewProps::events` never has the bit, so `shouldEmitPointerEvent` returns
 * false and the event is dropped in C++ -- silently, and only for the three
 * that were left out.
 *
 * The effect on a phone is nothing, because no phone has a mouse button to
 * press. The effect here is that a right-click cannot be answered at all, which
 * is what this platform needed them for; see core/PointerButtons.h.
 *
 * Reported rather than forked: the whole file is React Native's, this adds five
 * keys, and it goes away when upstream adds them. See plan/backlog.md.
 *
 * @format
 */

'use strict';

// Android's, for the reason every self-importing shim here is answered with
// Android's: these platforms report `PlatformConstantsAndroid` from C++, share
// ReactCommon's prop parsing, and drive the same components Android's
// JavaScript drives.
import BaseViewConfig from 'react-native/Libraries/NativeComponent/BaseViewConfig.android';

export default {
  ...BaseViewConfig,
  validAttributes: {
    ...BaseViewConfig.validAttributes,
    // The three registered events upstream does not declare, with their capture
    // phases. `onPointerCancel` is here for completeness rather than because
    // anything emits one yet: a desktop pointer is cancelled by the window
    // losing capture, which the hosts do not report.
    onPointerDown: true,
    onPointerDownCapture: true,
    onPointerUp: true,
    onPointerUpCapture: true,
    onPointerCancel: true,
    onPointerCancelCapture: true,
  },
};
