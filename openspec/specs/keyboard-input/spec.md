# keyboard-input Specification

## Purpose

Reaching things with the keyboard, and typing into them -- which a desktop
expects and a phone largely does not.

## Requirements

### Requirement: Tab moves focus and Enter activates

The system SHALL move focus between focusable views with Tab and Shift-Tab, and
SHALL activate the focused view with Enter or Space.

A React Native view is not a native control on these hosts, so the focus chain
is the platform's own rather than the toolkit's.

#### Scenario: Tab reaches a pressable view

- **WHEN** the person presses Tab
- **THEN** focus moves to the next focusable view and is shown

#### Scenario: Enter presses the focused view

- **WHEN** a pressable view has focus and Enter is pressed
- **THEN** its press handler runs

#### Scenario: Controls are reachable

- **WHEN** the person tabs through a screen containing a switch
- **THEN** the switch takes focus and can be toggled from the keyboard

### Requirement: Typing round-trips through React

The system SHALL deliver characters to the focused text field, and SHALL let a
controlled field's value come back down from React without losing keystrokes.

#### Scenario: A controlled field shows what was typed

- **WHEN** the person types into a controlled `<TextInput>`
- **THEN** the change reaches React and the value React returns is displayed

### Requirement: Escape reaches what is listening

The system SHALL deliver a key that nothing needs focus to receive, so that a
`<Modal>` closes on Escape.

#### Scenario: Escape closes a modal

- **WHEN** a modal is open and Escape is pressed
- **THEN** the modal's request-close handler runs
