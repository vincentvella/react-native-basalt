# Tasks

## 1. The seam

- [ ] Add a display model and lookup to `core/WindowControl.h`
- [ ] Add a change listener, following the window bounds listener

## 2. Hosts

- [ ] GTK: `GdkDisplay` and its monitors, plus the monitors-changed signal
- [ ] AppKit: `NSScreen.screens` and `NSApplicationDidChangeScreenParametersNotification`
- [ ] Win32: `EnumDisplayMonitors` and `WM_DISPLAYCHANGE`

## 3. JavaScript and tests

- [ ] `useDisplays()` and an imperative half, exported from `react-native-basalt`
- [ ] Demo in `js/`, scenario asserting at least one display with a sane scale factor
