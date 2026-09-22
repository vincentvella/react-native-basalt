# Spec Delta

## ADDED Requirements

### Requirement: An application can refuse to quit

The system SHALL let an app be asked before the application terminates, and
SHALL keep the application running when the app declines.

An app without a quit handler SHALL terminate exactly as it does now. The
harness's own shutdown SHALL NOT be refusable, so that a test for refusing can
still end.

#### Scenario: A guarded application does not quit

- **WHEN** the person quits an app that registered a quit handler
- **THEN** the application keeps running and the app is told it was asked

#### Scenario: The app agrees and the application terminates

- **WHEN** the handler calls the `quit` it was given
- **THEN** the application terminates

#### Scenario: Session end asks the same question

- **WHEN** the desktop is logging out or shutting down
- **THEN** the same handler is asked before the application ends
