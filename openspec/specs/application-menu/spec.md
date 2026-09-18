# application-menu Specification

## Purpose

The menu bar, where a desktop has one -- and the thing its absence quietly
breaks on macOS.

## Requirements

### Requirement: An app describes its menu bar in React

The system SHALL install a native menu bar from a declarative description of
submenus, items, separators and roles, and SHALL report an item the app owns
when it is chosen.

An item MAY carry an accelerator, MAY be disabled, and MAY be a role the
platform implements itself rather than a command the app handles.

#### Scenario: A described menu becomes a real menu

- **WHEN** an app renders a menu with submenus, items and separators
- **THEN** the platform's own menu reports the same structure back
- **AND** the shortcuts the platform attaches to its roles are present

#### Scenario: Choosing an app's own item reaches it

- **WHEN** the person chooses an item the app defined
- **THEN** that item's handler runs

### Requirement: Role items make editing shortcuts work on macOS

The system SHALL provide an Edit menu built from roles even when the app renders
no menu of its own.

AppKit routes every key equivalent through the main menu before the responder
chain sees it, so without this a `<TextInput>` cannot be copied from.

#### Scenario: Copy works in a text field

- **WHEN** an app with a text field runs on macOS
- **THEN** the standard editing shortcuts operate on that field

### Requirement: A platform without a menu bar says so

The system SHALL report whether this desktop puts a menu bar anywhere a person
can see it, and SHALL NOT emulate one where it does not.

GNOME's guidelines have recommended a header bar with a menu button since GNOME
3, and GTK4 removed the menu bar widget; that is a settled difference rather
than a gap.

#### Scenario: Linux reports no menu bar

- **WHEN** an app asks whether the application menu is supported on GTK
- **THEN** the answer is false
