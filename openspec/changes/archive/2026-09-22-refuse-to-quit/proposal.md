# An application can refuse to quit

## Why

An app can guard every window it owns and still lose the person's work to Cmd-Q.
Quitting is a different event from closing a window: macOS routes it through
`applicationShouldTerminate:` and never asks any window whether it minds, and
the Windows and GNOME session-end signals do the same. `useCloseRequest()`
closed the window half of this and left the larger half open.

## What Changes

- An app can register a quit handler and be asked before the application
  terminates, the way it can for a window.
- The decision is registered in advance, for the same reason as the window case:
  the system wants a synchronous answer and the handler is on another thread.
- Session end -- logout, shutdown -- is reported through the same handler, since
  an app with unsaved work cares about it identically.

## Capabilities

### Modified Capabilities
- `window-lifecycle`

## Impact

- `core/WindowHost.h` gains a quit interception flag and listener, following the
  shape the close interception already uses.
- AppKit answers `applicationShouldTerminate:` with `NSTerminateLater` and
  replies once JavaScript has decided.
- Win32 answers `WM_QUERYENDSESSION`; GTK hooks the session manager.
- `BASALT_QUIT_AFTER_MS` must keep working: the harness's own shutdown cannot be
  refusable, which is the trap the window version already hit once.
