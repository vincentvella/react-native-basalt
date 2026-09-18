# packaging Specification

## Purpose

Turning a host and a bundle into something the desktop treats as an application.

## Requirements

### Requirement: One command builds and runs an app per desktop

The system SHALL provide `run-linux`, `run-macos` and `run-windows` as one
command with three names, each bundling the app and launching the host.

#### Scenario: An app runs from one command

- **WHEN** a developer runs the command for their desktop
- **THEN** the app is bundled and the host launches it

### Requirement: An app is packaged as the desktop expects

The system SHALL give the application the identity its desktop requires.

On macOS that is a real `.app` with `Info.plist`, an identifier, declared URL
schemes and an ad-hoc signature -- without which macOS will not grant
notification permission at all. On Linux it is a `.desktop` entry. On Windows it
is an AppUserModelID and a Start Menu shortcut carrying it.

#### Scenario: macOS gets a bundle it will trust

- **WHEN** an app is packaged for macOS
- **THEN** it is a bundle with an identifier and an ad-hoc signature

#### Scenario: Packaging does not change the developer's session

- **WHEN** an app is packaged for Linux
- **THEN** a `.desktop` entry is written and not installed into the session

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
