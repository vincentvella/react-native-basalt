# expo-runtime Specification

## Purpose

Running an Expo app: the runtime object Expo's JavaScript looks for, the modules
behind it, and the view configs that turn an Expo view into a React Native
component.

An Expo app is the realistic test of this platform, because it is what people
actually write. Expo's JavaScript reaches for `globalThis.expo` at module scope,
so a missing runtime is not a degraded app but an app that does not start.

## Requirements

### Requirement: An Expo app starts

The system SHALL install the runtime object Expo's JavaScript expects before the
bundle evaluates, so that importing Expo's own packages does not throw.

#### Scenario: Expo's own packages import

- **WHEN** an app imports Expo packages and renders
- **THEN** the app starts and renders rather than throwing at module scope

### Requirement: Ported Expo modules answer honestly

The system SHALL provide native modules for the Expo APIs it implements, and
SHALL leave a method absent rather than stubbed where the platform cannot
answer -- so that Expo's own "not available on this platform" check reports the
method by name instead of an app believing a null.

#### Scenario: A ported module works

- **WHEN** an app calls an Expo API this platform implements
- **THEN** it answers with the platform's real behaviour

#### Scenario: An unimplemented method is absent

- **WHEN** an Expo API has no implementation on this desktop
- **THEN** the method is absent, so Expo reports it as unavailable by name

#### Scenario: A constant is true or missing

- **WHEN** an app reads a value Expo spreads into its own constants
- **THEN** the value is accurate for this desktop, or absent, and never invented

### Requirement: An Expo view is a Fabric component

The system SHALL answer `getViewConfig` for the Expo views it implements, naming
exactly the props and events that reach C++ -- because a prop absent from the
config is dropped in JavaScript before anything can diff it.

A view's native methods object SHALL be present even when it is empty, which is
the difference between "this view has no extra methods" and a TypeError.

#### Scenario: An Expo view mounts and draws

- **WHEN** an app renders an Expo view this platform implements
- **THEN** it mounts as a Fabric component and draws

#### Scenario: Only the props that travel are declared

- **WHEN** a view config is requested
- **THEN** it lists the props this platform parses, and no others

### Requirement: A capability package registers its own Expo modules

The system SHALL let a package outside core contribute Expo modules, discovered
from the app's own dependencies at build time, so that an optional capability is
an install rather than an edit to core.

#### Scenario: An installed capability package is reachable

- **WHEN** an app depends on a capability package and builds
- **THEN** that package's Expo modules are registered and answer

#### Scenario: A capability absent from a desktop no-ops

- **WHEN** a capability package is installed on a desktop that cannot provide it
- **THEN** its modules are present and answer honestly rather than being missing
