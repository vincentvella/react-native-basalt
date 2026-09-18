# appearance Specification

## Purpose

Light and dark, and following the desktop when it changes.

## Requirements

### Requirement: An app follows the desktop's colour scheme

The system SHALL report whether the desktop is in light or dark mode, and SHALL
tell an app when that changes while it is running.

#### Scenario: The scheme is readable at startup

- **WHEN** an app reads the colour scheme
- **THEN** it matches what the desktop is set to

#### Scenario: A change while running reaches the app

- **WHEN** the desktop switches between light and dark
- **THEN** an app listening for appearance changes re-renders
