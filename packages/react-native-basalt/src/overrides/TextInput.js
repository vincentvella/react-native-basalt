/**
 * `<TextInput>`, for every desktop platform here.
 *
 * React Native's own `TextInput.js` cannot be used here. Its body is
 *
 *     if (Platform.OS === 'android') { ... }
 *     else if (Platform.OS === 'ios') { ... }
 *
 * and on any other platform neither branch runs, so the component it goes on to
 * render is undefined and React reports an invalid element type. There is no
 * third branch to add without forking fifteen hundred lines of iOS and Android
 * behaviour that would then drift from upstream in silence.
 *
 * So this is a smaller implementation, and honestly a subset. It renders the
 * same native component the iOS path renders, `RCTSinglelineTextInputView`,
 * which ReactCommon rewrites to the component named `TextInput`, whose shadow
 * node is React Native's own iOS one and whose measurement runs through
 * whichever text layout manager the platform installs -- Pango on Linux, and
 * nothing yet on macOS, which is why `<TextInput>` does not mount there. What it does not carry is
 * the years of platform-specific behaviour in the real file: no
 * `InputAccessoryView`, no shared `TextInputState` focus registry, no autofill,
 * no `rejectResponderTermination`, no multiline.
 *
 * `plan/backlog.md` tracks what is missing. The seam is here when it is time.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {useCallback, useImperativeHandle, useRef, useState} from 'react';
import RCTSinglelineTextInputNativeComponent, {
  Commands,
} from 'react-native/Libraries/Components/TextInput/RCTSingelineTextInputNativeComponent';
import StyleSheet from 'react-native/Libraries/StyleSheet/StyleSheet';

/**
 * React Native's text fields are controlled: JavaScript owns the value and
 * every keystroke round-trips through a re-render. `mostRecentEventCount` is
 * how the native side tells a stale prop from a current one. Without it a fast
 * typist outruns the render loop and watches characters reorder themselves.
 */
function TextInput(
  {
    value,
    defaultValue,
    onChange,
    onChangeText,
    onFocus,
    onBlur,
    editable,
    style,
    ...rest
  },
  forwardedRef,
) {
  const inputRef = useRef(null);
  const [mostRecentEventCount, setMostRecentEventCount] = useState(0);
  const focused = useRef(false);

  useImperativeHandle(
    forwardedRef,
    () => ({
      focus() {
        if (inputRef.current != null) {
          Commands.focus(inputRef.current);
        }
      },
      blur() {
        if (inputRef.current != null) {
          Commands.blur(inputRef.current);
        }
      },
      clear() {
        if (inputRef.current != null) {
          Commands.setTextAndSelection(
            inputRef.current,
            mostRecentEventCount,
            '',
            0,
            0,
          );
        }
        onChangeText?.('');
      },
      setSelection(start, end) {
        if (inputRef.current != null) {
          Commands.setTextAndSelection(
            inputRef.current,
            mostRecentEventCount,
            null,
            start,
            end,
          );
        }
      },
      isFocused() {
        return focused.current;
      },
    }),
    [mostRecentEventCount, onChangeText],
  );

  const handleChange = useCallback(
    event => {
      const {text, eventCount} = event.nativeEvent;
      setMostRecentEventCount(eventCount);
      onChange?.(event);
      onChangeText?.(text);
    },
    [onChange, onChangeText],
  );

  const handleFocus = useCallback(
    event => {
      focused.current = true;
      onFocus?.(event);
    },
    [onFocus],
  );

  const handleBlur = useCallback(
    event => {
      focused.current = false;
      onBlur?.(event);
    },
    [onBlur],
  );

  return (
    <RCTSinglelineTextInputNativeComponent
      {...rest}
      ref={inputRef}
      style={[styles.input, style]}
      // `text` rather than `value`: the iOS view config calls it that, and this
      // renders the iOS component. An uncontrolled field is given its default
      // once and then left alone, which is what passing undefined does.
      text={value != null ? value : defaultValue}
      editable={editable !== false}
      mostRecentEventCount={mostRecentEventCount}
      onChange={handleChange}
      onFocus={handleFocus}
      onBlur={handleBlur}
    />
  );
}

const styles = StyleSheet.create({
  input: {
    // Yoga decides the size, and a field measured from empty text is a couple
    // of pixels tall -- invisible, unclickable, and indistinguishable from the
    // component being broken. Any explicit height in `style` overrides this.
    minHeight: 36,
  },
});

export default React.forwardRef(TextInput);
