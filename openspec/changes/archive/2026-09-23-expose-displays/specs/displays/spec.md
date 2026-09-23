# Spec Delta

## Purpose

What screens the desktop has, where they are, and how dense they are.

## ADDED Requirements

### Requirement: An app can enumerate the displays

The system SHALL report every display the desktop has, each with its bounds, its
work area, its scale factor, and whether it is the primary display, in the same
logical pixels the window API uses.

#### Scenario: An app reads the display list

- **WHEN** an app asks for the displays
- **THEN** it gets one entry per display, with bounds and scale factor

### Requirement: An app is told when the arrangement changes

The system SHALL report displays being added, removed or rearranged while the
app is running.

#### Scenario: Plugging in a monitor reaches the app

- **WHEN** a display is connected or disconnected
- **THEN** an app listening for display changes is told

### Requirement: An app can read the pointer position

The system SHALL report the pointer's position in the desktop's coordinates.

#### Scenario: Reading the pointer position

- **WHEN** an app asks where the pointer is
- **THEN** it gets a point in the same coordinates the display bounds use
