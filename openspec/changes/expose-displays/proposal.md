# Tell an app about the displays it is on

## Why

The platform already asks about displays -- `NSScreen` and `MonitorFromWindow`
are how `center()` and full screen work -- so the answer exists and is simply
not passed on. An app cannot get a display list, a per-display scale factor or
work area, the pointer's position, or any notice when a monitor is plugged in or
the arrangement changes. React Native's `Dimensions` reports the window, which
is the right answer to a different question.

This is cheaper than most of the backlog precisely because the lookups are
already written.

## What Changes

- A display list, each with its bounds, work area, scale factor, and whether it
  is the primary one.
- The pointer's position in desktop coordinates.
- An event when displays are added, removed or rearranged.

## Capabilities

### New Capabilities
- `displays`

## Impact

- `core/WindowControl.h` gains a display seam beside the window one; the three
  window controls already call the platform APIs it needs.
- No JavaScript API in React Native to follow, so this invents one -- Electron's
  `screen` module is the closest reference.
