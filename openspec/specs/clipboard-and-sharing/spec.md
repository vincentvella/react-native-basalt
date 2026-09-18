# clipboard-and-sharing Specification

## Purpose

Getting text out of an app and into the rest of the desktop.

## Requirements

### Requirement: An app can read and write the clipboard

The system SHALL read and write text on the platform's clipboard, through React
Native's own `Clipboard` API.

#### Scenario: Text written is readable

- **WHEN** an app writes a string to the clipboard
- **THEN** reading the clipboard answers with that string

### Requirement: An app can share

The system SHALL implement `Share.share()` and SHALL settle it both ways --
reporting what the person chose, or that they dismissed it.

React Native's own `Share` refuses any platform that is not iOS or Android
before reaching a native module, so this platform answers it rather than
inheriting it.

#### Scenario: Sharing reports the choice

- **WHEN** an app shares and the person picks a target
- **THEN** the promise resolves describing what happened

#### Scenario: Dismissing settles rather than hanging

- **WHEN** the person dismisses the share UI
- **THEN** the promise settles as dismissed
