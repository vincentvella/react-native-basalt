# accessibility Specification

## Purpose

What a screen reader is told about the tree, on all three desktops.

React Native's accessibility props are one vocabulary and each desktop has its
own -- AT-SPI, NSAccessibility, UI Automation. The mapping is a platform
decision and lives beside each platform; what is required of all three is that
the same React tree produces the same answers.

## Requirements

### Requirement: A component reports what it is

The system SHALL map React Native's `accessibilityRole` onto the desktop's own
role vocabulary, SHALL infer a role for components that have an obvious one, and
SHALL report the role in the tree dump so the three hosts can be compared.

A role it does not recognise SHALL fall back to the platform's generic grouping
role rather than being dropped, because an unknown role is still an element.

#### Scenario: A role reaches the platform

- **WHEN** a view sets `accessibilityRole`
- **THEN** the platform's accessible object reports the matching native role
- **AND** the tree dump reports React Native's own spelling of it

#### Scenario: Text and images name themselves

- **WHEN** a `<Text>` or an `<Image>` is mounted without an explicit role
- **THEN** it reports itself as text or as an image

#### Scenario: An unknown role is still an element

- **WHEN** a view sets a role this platform has no mapping for
- **THEN** it reports the generic grouping role rather than no role at all

#### Scenario: A plain view is not an element

- **WHEN** a `<View>` has no role, label or hint
- **THEN** it does not appear to assistive technology as its own element

### Requirement: A component carries its name and description

The system SHALL pass `accessibilityLabel` to the platform as the accessible
name and `accessibilityHint` as its description.

Where a component is drawn by a native peer rather than by the view itself --
a `<TextInput>`'s editable, a control -- the name SHALL land on the peer, which
is the object assistive technology reaches.

#### Scenario: A label and a hint reach the platform

- **WHEN** a view sets `accessibilityLabel` and `accessibilityHint`
- **THEN** the accessible object reports them as its name and its description

#### Scenario: A field's label lands on the field

- **WHEN** a `<TextInput>` sets `accessibilityLabel`
- **THEN** the name is on the editable peer rather than on the wrapper view

### Requirement: State is reported, and unset state is left alone

The system SHALL map `accessibilityState` -- disabled, checked, selected,
expanded, busy -- onto the platform's own state.

Each is a tri-state: a state the app did not set SHALL leave the platform's
default untouched, which is not the same as setting it false. A `checked` of
false says "this is a checkbox and it is unchecked"; no `checked` at all says
"this is not a checkbox".

#### Scenario: A set state reaches the platform

- **WHEN** a view sets a state in `accessibilityState`
- **THEN** the accessible object reports it

#### Scenario: An unset state changes nothing

- **WHEN** a view sets no `accessibilityState`
- **THEN** the platform's own default for each state is left as it was

### Requirement: A subtree can be hidden from assistive technology

The system SHALL honour `accessibilityElementsHidden`,
`importantForAccessibility: 'no-hide-descendants'` and `accessible={false}` by
taking the view, and everything inside it, out of the accessibility tree.

#### Scenario: A hidden view and its children disappear

- **WHEN** a view is marked hidden from assistive technology
- **THEN** neither it nor any of its descendants is reachable
