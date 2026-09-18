# window-title-bar Specification

## Purpose

The strip at the top of a window: its title, its colours, and whether the system
draws it or the app does.

## Requirements

### Requirement: An app can title and colour its title bar

The system SHALL let an app set the window's title and the title bar's
background, text and border colours.

Requests SHALL stack the way `<StatusBar>`'s do: the most recently mounted wins,
key by key, and unmounting one restores whatever was beneath it.

#### Scenario: A mounted request takes effect

- **WHEN** an app renders a title bar request
- **THEN** the window's title and colours change to match

#### Scenario: Unmounting restores the request beneath

- **WHEN** a request that overrode another unmounts
- **THEN** the earlier request's values apply again

### Requirement: An app can draw its own header

The system SHALL offer a hidden title bar style, in which the window has no
system title bar, the app's content reaches the top of the window, and the host
continues to draw the minimise, maximise and close buttons over it.

The app SHALL be able to say which parts of its header drag the window and which
do not, and SHALL be told the height and button width it must leave clear.

#### Scenario: Hidden style gives the app the top of the window

- **WHEN** an app asks for the hidden style
- **THEN** its content occupies the full height of the window
- **AND** the caption buttons are still drawn and still work

#### Scenario: The app learns what to leave clear

- **WHEN** an app reads the title bar metrics
- **THEN** it gets the height and the width the buttons occupy

### Requirement: A host without a title bar module ignores the request

The system SHALL accept every title bar call on a host that does not implement
one, and SHALL report zero metrics there, so that the same code runs on every
desktop.

#### Scenario: The same app runs unchanged where it is unimplemented

- **WHEN** an app using the title bar runs on a host with no implementation
- **THEN** nothing throws and the metrics are zero
