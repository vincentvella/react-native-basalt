# Tasks

## 1. The seam

- [ ] Add quit interception to `core/WindowHost.h`, following `setHostWindowCloseIntercepted`
- [ ] Emit a device event for the attempt, and a way to answer it

## 2. Hosts

- [ ] AppKit: answer `applicationShouldTerminate:` with `NSTerminateLater`, reply on the decision
- [ ] Win32: answer `WM_QUERYENDSESSION`, and keep Alt+F4 on the main window working
- [ ] GTK: hook the session manager; record what it cannot do

## 3. JavaScript and tests

- [ ] `useQuitRequest(handler)`, exported from `react-native-basalt`
- [ ] Ensure `BASALT_QUIT_AFTER_MS` bypasses the interception
- [ ] Demo in `js/`, scenario asserting a refused quit and an agreed one
