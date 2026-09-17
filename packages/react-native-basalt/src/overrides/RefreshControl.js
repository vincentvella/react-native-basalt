/**
 * `RefreshControl`, for a desktop React Native platform.
 *
 * Not a shim. React Native's own file branches on `Platform.OS === 'ios'` with
 * an `else` that renders `AndroidSwipeRefreshLayout` -- a component whose
 * shadow node needs fbjni and which no desktop registers -- so on `linux`,
 * `macos` or `windows` the app mounts a component nothing can put on screen.
 * That is the third time this has happened (`TextInput`, `Alert`, `Share`), and
 * the answer here is the same: render the iOS-shaped component, which is
 * ordinary portable C++ all the way down.
 *
 * Two differences from the original, both deliberate.
 *
 * The imperative `setNativeRefreshing` command is gone. It exists because
 * UIKit's own refresh control moves on its own and has to be told to stop; the
 * desktop control draws only what `refreshing` says, so the prop is the only
 * path and the controlled-component bookkeeping has nothing to do.
 *
 * The control has a height while it is refreshing and none otherwise. On a
 * phone the spinner lives in the rubber band above the list, which a desktop
 * scroll view does not have; here it takes a row at the top of the content and
 * gives it back, which is what a web app does and what the pull gesture in
 * `core/PullToRefresh.h` is counting towards. The number matches
 * `kRefreshControlHeight` there.
 *
 * @format
 */

'use strict';

import PullToRefreshViewNativeComponent from 'react-native-basalt/upstream/Libraries/Components/RefreshControl/PullToRefreshViewNativeComponent';
import * as React from 'react';

// core/DesktopControls.h's kRefreshControlHeight. Both numbers describe the
// same row and neither can move without the other.
const REFRESH_CONTROL_HEIGHT = 40;

export default class RefreshControl extends React.Component {
  render() {
    // `colors`, `progressBackgroundColor`, `size` and `enabled` are Android's
    // half of the prop type and mean nothing to this component, exactly as
    // they mean nothing to the iOS one.
    const {
      enabled,
      colors,
      progressBackgroundColor,
      size,
      style,
      ...props
    } = this.props;

    return (
      <PullToRefreshViewNativeComponent
        {...props}
        style={[
          {
            height: this.props.refreshing === true ? REFRESH_CONTROL_HEIGHT : 0,
            alignItems: 'center',
            justifyContent: 'center',
          },
          style,
        ]}
        onRefresh={this._onRefresh}
      />
    );
  }

  _onRefresh = () => {
    this.props.onRefresh && this.props.onRefresh();
  };
}
