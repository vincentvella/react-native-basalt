# Spec Delta

## ADDED Requirements

### Requirement: One command configures an existing app for the desktop

The system SHALL provide a command that adds desktop support to an app that
already exists, performing every configuration step this platform needs rather
than documenting them.

The command SHALL be idempotent: run against an already-configured app it SHALL
report what is already correct and change nothing.

The command SHALL refuse an app it does not recognise, naming what it expected,
rather than writing a partial configuration -- a half-configured app fails later
and further away than one that was refused.

#### Scenario: A fresh app becomes buildable

- **WHEN** the command is run in an app that has no desktop configuration
- **THEN** the app can afterwards be bundled and built for each supported
  desktop with no further manual edits

#### Scenario: Running it twice changes nothing

- **WHEN** the command is run again on an app it has already configured
- **THEN** it reports the configuration as already present and writes nothing

#### Scenario: An unrecognised project is refused

- **WHEN** the command is run somewhere it cannot identify as an app
- **THEN** it stops, names what it expected to find, and leaves the directory
  untouched
