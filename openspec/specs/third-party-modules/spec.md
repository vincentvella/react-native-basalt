# third-party-modules Specification

## Purpose

Libraries that ship Objective-C, Java and Kotlin, and what it takes for them to
work here.

This is the claim the architecture makes: a third-party native module is a
TurboModule or a Fabric component with C++ codegen, so porting one is writing a
backend rather than reimplementing a library. gesture-handler and Reanimated are
the two that prove or disprove it, because everything else depends on them.

## Requirements

### Requirement: A gesture library's contract is honoured, not its code

The system SHALL implement the behaviour a gesture library's JavaScript depends
on -- the handler state machine, the events, and the relations between handlers
-- rather than porting the library's platform code.

A pan SHALL begin on contact and activate once it has moved far enough; a tap
SHALL be a press that neither moved nor was held too long, and SHALL fail rather
than hang when it cannot happen.

#### Scenario: A pan activates once it has moved

- **WHEN** a pointer presses and then moves past the threshold
- **THEN** the handler begins, then activates
- **AND** its translation is measured from where the gesture started

#### Scenario: A tap is a press that did not move or linger

- **WHEN** a pointer presses and releases without wandering or being held
- **THEN** the tap handler activates
- **AND** a press that wanders, or is held too long, fails instead

#### Scenario: A position is reported in the handler's own coordinates

- **WHEN** a gesture reaches a handler
- **THEN** the pointer's position is relative to that handler's view

### Requirement: Handlers resolve conflicts between themselves

The system SHALL cancel every other handler tracking the same pointer when one
activates, unless the two were declared simultaneous, and SHALL hold a handler
that was told to wait until the handler it waits for has failed.

#### Scenario: An activating handler cancels the others

- **WHEN** one handler activates while others track the same pointer
- **THEN** the others are cancelled

#### Scenario: Simultaneous handlers are left alone

- **WHEN** two handlers are declared simultaneous
- **THEN** one activating does not cancel the other

#### Scenario: A handler waits for the one it was told to wait for

- **WHEN** a handler is declared to wait for another
- **THEN** it stays pending until that one has failed

#### Scenario: A gesture on an ancestor still arrives

- **WHEN** the pressed view has no handler but an ancestor does
- **THEN** the ancestor's handler is offered the gesture

### Requirement: An animation library runs its worklets

The system SHALL provide the runtime a worklet-based animation library needs, so
that animations declared in JavaScript run without the library's own platform
code.

#### Scenario: An animated component animates

- **WHEN** an app renders a component animated by a worklet library
- **THEN** the animation runs

### Requirement: A dropped handler stops hearing about the pointer

The system SHALL stop offering a gesture to a handler whose view has been
unmounted, and SHALL never offer one to a handler attached to nothing.

#### Scenario: Unmounting detaches a handler

- **WHEN** a view carrying a handler is unmounted mid-gesture
- **THEN** that handler receives nothing further
