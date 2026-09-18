# window-lifecycle Specification

## Purpose

Opening, closing and refusing to close a window, on GTK4, AppKit and Win32.

React Native has no API for any of this: a phone has one window, it is the
screen, and nothing an app says would change it. A window here is a Fabric
surface and so a React root, which is the grain the rest of this follows from.

## Requirements

### Requirement: A second window is a second React root

The system SHALL open a window per `<Window>` mounted, running the component in
its own Fabric surface, and SHALL close that window when the element unmounts.

State and callbacks SHALL cross between the trees; React context SHALL NOT,
because the children render in a different root.

#### Scenario: Mounting opens a window that renders its own tree

- **WHEN** an app mounts `<Window>` with children
- **THEN** a new window opens and its surface renders those children
- **AND** the tree dump reports it under its own window header

#### Scenario: State is shared across the two trees

- **WHEN** a control in the second window calls a setter that lives in the
  first window's tree
- **THEN** both windows re-render with the new value

#### Scenario: Input is routed to the window it landed in

- **WHEN** a press lands in the second window
- **THEN** it is hit-tested against that window's view tree and not the first's

#### Scenario: Unmounting closes the window

- **WHEN** the `<Window>` element unmounts
- **THEN** the window closes and its surface stops

### Requirement: A window the person closed tells the app

The system SHALL report a window closed by the person -- its own close button,
or the window manager -- to the `<Window>` that opened it, and SHALL NOT report
a window the app itself closed.

#### Scenario: Closing from the window manager reaches onClose

- **WHEN** the person closes a second window from its close button
- **THEN** `onClose` is called
- **AND** the host no longer holds a record for that window

### Requirement: A window can refuse to close

The system SHALL let an app be asked before a window closes, for a window it
opened and for the window the app itself runs in.

A window with a close handler SHALL refuse every close the window manager asks
for and report the attempt instead. A window without one SHALL close exactly as
it would have.

The decision SHALL be registered in advance rather than returned from the
handler, because the window manager requires a synchronous answer and the
handler runs on the JavaScript thread.

#### Scenario: A guarded window does not close

- **WHEN** the person closes a window whose app passed `onCloseRequest`
- **THEN** the window stays open
- **AND** the app is told, and can re-render knowing it was asked

#### Scenario: The app agrees and the window goes

- **WHEN** the handler calls the `close` it was given
- **THEN** the interception is lifted and the window closes

#### Scenario: The app's own window can refuse too

- **WHEN** the person closes the window the app runs in, and the app registered
  `useCloseRequest`
- **THEN** the window stays open and the app keeps running

#### Scenario: An unguarded window is unaffected

- **WHEN** no close handler is registered
- **THEN** the close proceeds without waiting for JavaScript
