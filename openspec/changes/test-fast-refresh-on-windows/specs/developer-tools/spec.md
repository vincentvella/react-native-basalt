# Spec Delta

## MODIFIED Requirements

### Requirement: Edits reach a running app

The system SHALL apply an edit to a running app without restarting it, on every
desktop, and this SHALL be verified by an automated scenario on every desktop
rather than by hand.

Where the environment a test runs in cannot observe a file change -- which is a
property of the runner rather than of this platform -- the scenario SHALL still
assert the half that does not depend on it, rather than being skipped whole.

#### Scenario: Editing a file updates the running app

- **WHEN** a developer edits a component while the app runs against Metro
- **THEN** the change appears without a full restart

#### Scenario: Every desktop is covered by the scenario

- **WHEN** the end-to-end suite runs on any of the three desktops
- **THEN** the Fast Refresh scenario runs there rather than being skipped for
  want of a way to start the packager

#### Scenario: A packager that never saw the edit is reported as such

- **WHEN** the packager does not observe the edit at all
- **THEN** the scenario says so, distinguishing it from an app that was served
  the edit and failed to apply it
