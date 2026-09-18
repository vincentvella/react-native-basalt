# Spec Delta

## ADDED Requirements

### Requirement: A package can contribute native code to the host build

The system SHALL discover packages that declare native code in their own
manifest and compile them into the host, without the command-line tool knowing
those packages by name.

A package that declares nothing SHALL be ignored, and a package whose native
code fails to build SHALL fail the build with its name rather than silently
producing a host without it.

#### Scenario: An installed capability package reaches the host

- **WHEN** an app installs a package declaring a native entry point and builds a
  host
- **THEN** that package's native code is compiled into the host
- **AND** its JavaScript can reach the module it registers

#### Scenario: A package that declares nothing changes nothing

- **WHEN** an app installs a package with no native declaration
- **THEN** the host builds exactly as it would have

### Requirement: Core is the application's own surface

The system SHALL keep in `react-native-basalt` the application's own surface --
its windows, menus, dialogs, title bar, components, input, and the APIs React
Native itself exposes.

A capability SHALL live in its own package when it needs the person's consent,
when it touches hardware or another application, or when it acts outside the
app's own windows.

#### Scenario: A permission-gated capability is not in core

- **WHEN** a capability requires an operating-system permission prompt
- **THEN** it ships as its own package rather than in core

#### Scenario: Ordinary window furniture stays in core

- **WHEN** a capability is part of the app's own window -- a menu, a dialog, a
  title bar
- **THEN** it stays in core, because a platform that made you install a package
  for a menu would be worse rather than smaller
