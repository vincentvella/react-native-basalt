# Spec Delta

## Purpose

Accepting something dragged onto the application, and letting the application's
own content be dragged out of it.

## ADDED Requirements

### Requirement: A view can accept what is dragged onto it

The system SHALL let a view declare the payload types it accepts, and SHALL tell
it when a drag enters it, moves over it, leaves it, or is dropped on it.

The innermost accepting view under the pointer SHALL be the one told.

#### Scenario: Dropping a file on a view reaches the app

- **WHEN** the person drags a file from the file manager onto an accepting view
- **THEN** that view is told a drag entered, and on release is given the path

#### Scenario: A view that accepts nothing is not offered the drag

- **WHEN** a drag passes over a view declaring no accepted types
- **THEN** the drag is offered to the nearest ancestor that does accept it

#### Scenario: Leaving without dropping is reported

- **WHEN** a drag enters a view and then leaves without being released
- **THEN** the view is told the drag left and no drop is reported

### Requirement: A view can be dragged out of the application

The system SHALL let a view declare what it represents and begin a drag, so that
another application receives it.

#### Scenario: Dragging content to another application

- **WHEN** the person drags a view that declares a payload
- **THEN** another application receiving the drop is given that payload
