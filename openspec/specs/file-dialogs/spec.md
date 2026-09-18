# file-dialogs Specification

## Purpose

The native open, save and choose-a-folder dialogs. React Native has no API for
these because a phone has none.

## Requirements

### Requirement: An app can open, save and choose a folder

The system SHALL present the platform's own file dialogs and SHALL answer with
the paths chosen, or with a cancellation.

The answer SHALL have one shape for all three: a cancelled flag and a list of
paths, so that an app moving between them is not also moving between result
types. A save dialog answers with a list of one.

A dialog SHALL accept a title, a starting directory, a default name, and filters
given as a name and a list of extensions.

#### Scenario: Opening a file answers with its path

- **WHEN** the person chooses a file in the open dialog
- **THEN** the call answers with that path and a cancelled flag of false

#### Scenario: Cancelling is not an error

- **WHEN** the person dismisses the dialog
- **THEN** the call answers with a cancelled flag of true and no paths
- **AND** it does not reject

### Requirement: A dialog does not block the runtime

The system SHALL NOT run a dialog's modal loop on the JavaScript thread.

These are called from JavaScript, and a modal loop there would stop the runtime
for as long as the dialog is open.

#### Scenario: JavaScript keeps running while a dialog is open

- **WHEN** a dialog is shown
- **THEN** the call returns immediately with a promise
