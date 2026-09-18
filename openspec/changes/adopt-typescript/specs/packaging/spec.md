# Spec Delta

## ADDED Requirements

### Requirement: The package declares its own types

The system SHALL publish type declarations for its public API, so that an app
written in TypeScript -- which is what this platform's own installer produces --
gets types from the import rather than `any`.

The declarations SHALL be generated from the implementation rather than
maintained beside it, so the two cannot disagree.

Type checking SHALL run in continuous integration, so that a type that has
stopped describing the code fails a build rather than misleading a reader.

#### Scenario: An app gets types from the import

- **WHEN** a TypeScript app imports this platform's public API
- **THEN** the imported values are typed, and a wrong call is a type error

#### Scenario: The published package carries declarations

- **WHEN** the package is packed
- **THEN** it contains type declarations for its public entry points, and
  `package.json` names them

#### Scenario: Types that have gone stale fail the build

- **WHEN** an implementation changes so that its types no longer describe it
- **THEN** continuous integration fails
