# pointer-input Specification

## Purpose

What a mouse does: pressing, moving, hovering, scrolling, and which button it
was.

## Requirements

### Requirement: A press is hit-tested and delivered to React

The system SHALL deliver a primary press to the innermost view under the point,
through React Native's touch model, so that `onPress`, the responder system and
gesture handlers behave as they do on a phone.

Each window SHALL hit-test against its own view tree.

#### Scenario: A press runs the handler of the view under it

- **WHEN** the person presses a pressable view
- **THEN** that view's press handler runs

#### Scenario: `pointerEvents` decides what a press lands on

- **WHEN** a view sets `pointerEvents` to exclude itself or its children
- **THEN** the press lands on whatever the property says it should

### Requirement: Hovering is reported as pointer events

The system SHALL report a pointer moving with no button held as enter, leave,
over and out events, in the order the web defines, against the innermost view
and the chain above it.

A phone has no hover: this exists because a desktop does.

#### Scenario: Moving between nested views reports the whole sequence

- **WHEN** the pointer moves into one box, across to a sibling, and off the
  surface
- **THEN** enter, over, out and leave arrive for each, in order

### Requirement: Which button was pressed travels with the event

The system SHALL carry the button as a pointer event using the web's numbering,
and SHALL drive the touch model from the primary button alone.

React Native's touch model has no concept of which button, because a phone has
none. A secondary or middle click therefore produces a pointer event and no
touch.

#### Scenario: A middle or secondary click presses nothing

- **WHEN** the person clicks with a non-primary button
- **THEN** no press, gesture or responder interaction begins

### Requirement: A wheel scrolls

The system SHALL turn wheel input into scrolling of the scrollable view under
the pointer, with the sign a desktop expects.

#### Scenario: Turning the wheel scrolls a list

- **WHEN** the person turns the wheel over a scrollable view
- **THEN** the view scrolls and reports its offset to React
