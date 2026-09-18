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
