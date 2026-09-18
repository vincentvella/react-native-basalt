# window-geometry Specification

## Purpose

How big a window is, where it sits, whether it fills the screen, and how big it
is allowed to be -- and saying honestly which of those each desktop can do.

## Requirements

### Requirement: An app can read and change its window's geometry

The system SHALL let an app set the window's size, set its position, centre it,
make it full screen, minimise it, toggle maximised, and close it.

Size SHALL be a request rather than a command: a tiling window manager may
refuse, so the reported bounds are what happened and not what was asked for.

Every mutation SHALL be applied on the UI thread, because no toolkit here may be
touched from the JavaScript thread.

#### Scenario: A size request comes back as the size the window got

- **WHEN** an app calls `setSize(700, 500)`
- **THEN** the window resizes and `bounds` reports the size it actually became

#### Scenario: A state change that is not a resize is still reported

- **WHEN** the window goes full screen
- **THEN** `bounds.fullScreen` becomes true
- **AND** this holds on hosts where full screen changes a property or a style
  rather than the size

### Requirement: Bounds are live

The system SHALL re-render an app reading `bounds` whenever the window is
moved, resized, maximised or made full screen, by any cause -- the person, the
window manager, or the app.

#### Scenario: Dragging a corner updates the app

- **WHEN** the window is resized by anything at all
- **THEN** the app that read `bounds` renders with the new numbers

#### Scenario: Reading before there is a window does not throw

- **WHEN** an app reads `bounds` during its first render
- **THEN** it gets zeroes rather than null or an error

### Requirement: A window can be constrained

The system SHALL let an app set a minimum size, a maximum size, whether the
window may be resized, and whether it floats above other applications. Zero in
either direction SHALL clear that limit.

A size requested outside a limit the platform enforces SHALL be clamped before
it reaches the toolkit, because the toolkits disagree about whether a
programmatic resize is subject to the same limits a dragged corner is.

#### Scenario: A request below the minimum is raised

- **WHEN** an app sets a minimum of 500x400 and then asks for 300x200
- **THEN** the window is no smaller than 500x400

#### Scenario: A request above the maximum is lowered

- **WHEN** an app sets a maximum of 800x600 and then asks for 1400x1100
- **THEN** the window is no larger than 800x600
- **AND** this applies only where the platform reports that it enforces maxima

#### Scenario: A limit does not retroactively resize

- **WHEN** a limit is set while the window is already outside it
- **THEN** the window is not resized by the act of setting the limit

### Requirement: A desktop says what it cannot do

The system SHALL report which geometry operations this desktop actually
performs, so that an app can hide a control rather than offer one that does
nothing.

Position, minimum size, maximum size, resizability and always-on-top SHALL each
be reported. An operation reported as unavailable SHALL NOT be quietly emulated.

#### Scenario: Linux reports what GTK4 removed

- **WHEN** an app reads the capabilities on the GTK host
- **THEN** position, maximum size and always-on-top are false
- **AND** minimum size and resizability are true

#### Scenario: macOS and Windows report all of them

- **WHEN** an app reads the capabilities on the AppKit or Win32 host
- **THEN** every one of the five is true
