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

### Requirement: The core package is what an app cannot avoid

The system SHALL keep in `react-native-basalt` only what an app cannot ship
without, what implements an API React Native itself exposes, or what requires a
seam no package outside core could reach.

Every other capability SHALL live in its own package.

#### Scenario: A new capability is placed by the rule

- **WHEN** a capability is added to this platform
- **THEN** it goes in core only if it fails all three tests
- **AND** the reasoning is recorded with the capability
