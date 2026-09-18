# deep-links Specification

## Purpose

Opening a URL, and being opened by one.

## Requirements

### Requirement: An app can open a URL in the desktop

The system SHALL open a URL with whatever the desktop has registered for it, and
SHALL report whether a URL can be opened before trying.

#### Scenario: Opening a web URL hands it to the browser

- **WHEN** an app opens an `https` URL
- **THEN** the desktop's default handler receives it

### Requirement: An app is told the URL it was opened with

The system SHALL report the URL the application was launched with, and SHALL
declare the app's URL schemes to the desktop when packaging it.

#### Scenario: A cold start reports its URL

- **WHEN** the app is launched with a URL
- **THEN** `Linking.getInitialURL()` answers with it

#### Scenario: Packaging declares the schemes

- **WHEN** an app declaring URL schemes is packaged
- **THEN** those schemes are registered with the desktop
