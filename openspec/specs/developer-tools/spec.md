# developer-tools Specification

## Purpose

The things that make a platform workable while writing an app: the error
inspector, the developer menu, Fast Refresh, and React DevTools.

## Requirements

### Requirement: An error opens React Native's own inspector

The system SHALL run React Native's LogBox as a surface of its own, and SHALL
show it when JavaScript logs an error.

It is React Native's own JavaScript rather than anything this platform draws,
which is why it behaves as developers already expect.

#### Scenario: A console error opens the inspector

- **WHEN** an app logs an error
- **THEN** the inspector appears over the app and can be dismissed

### Requirement: A developer menu is reachable

The system SHALL offer React Native's developer menu on a keyboard shortcut, and
SHALL let it reload the app and toggle the element inspector.

#### Scenario: The shortcut opens the menu

- **WHEN** the developer presses the menu shortcut
- **THEN** the developer menu appears

#### Scenario: Reload restarts the app

- **WHEN** reload is chosen
- **THEN** the bundle is re-evaluated and the app restarts

### Requirement: Edits reach a running app

The system SHALL apply Fast Refresh from Metro to a running app.

#### Scenario: Editing a file updates the running app

- **WHEN** a developer edits a component while the app runs against Metro
- **THEN** the change appears without a full restart

### Requirement: DevTools can highlight what it inspects

The system SHALL draw React DevTools' overlays: a filled highlight for an
inspected element, and an outlined one for a trace update that clears itself.

#### Scenario: An inspected element is highlighted

- **WHEN** DevTools highlights an element
- **THEN** a highlight is drawn over it and stays until cleared

#### Scenario: A trace update takes itself down

- **WHEN** DevTools reports a trace update
- **THEN** its outline is drawn and disappears on its own
