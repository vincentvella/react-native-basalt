# component-surface Specification

## Purpose

The components an ordinary React Native app is built from, mounted on three
desktops from React Native's own JavaScript.

## Requirements

### Requirement: The core components mount and lay out

The system SHALL mount `<View>`, `<Text>`, `<Image>`, `<ScrollView>` and
`<TextInput>` from React Native's own implementations, laid out by Yoga and
diffed by Fabric.

Nothing in React Native SHALL be forked to achieve this: components come from
`react-native` exactly as they do on iOS and Android.

#### Scenario: An app renders its tree

- **WHEN** an app renders the core components
- **THEN** each is mounted with the frame Yoga computed

#### Scenario: An image can be recoloured

- **WHEN** an app sets `tintColor` on an `<Image>`
- **THEN** the image is drawn in that colour, keeping its own alpha
- **AND** every desktop reports the same colour for it

#### Scenario: A view turned away from the viewer is not drawn

- **WHEN** a view with `backfaceVisibility: 'hidden'` is given a transform that
  mirrors it
- **THEN** it is neither drawn nor hit tested, and nor are its children
- **AND** clearing either the prop or the transform shows it again

#### Scenario: Hiding a view has two independent reasons

- **WHEN** a view is hidden by `display: 'none'` and by a back face turned away
- **THEN** it stays hidden until both reasons are gone
- **AND** neither reason answers for the other when Fabric applies props and
  layout metrics in separate calls

#### Scenario: The three desktops agree

- **WHEN** the same app is rendered on two hosts
- **THEN** the resulting view trees match, frames included
- **AND** a view that is not drawn at all says so in the tree, rather than
  reading as a visible one

### Requirement: The controls mount and answer

The system SHALL mount `<Switch>`, `<ActivityIndicator>`, `<Modal>` and
`<RefreshControl>`, and each SHALL report the interactions React Native says it
should.

#### Scenario: A switch toggles and reports

- **WHEN** the person activates a switch
- **THEN** its change handler runs and the value React returns is displayed

#### Scenario: A modal opens over the app

- **WHEN** an app opens a modal
- **THEN** it covers the surface and closes on request

### Requirement: A component with no implementation renders visibly

The system SHALL render a placeholder for a component it does not implement,
rather than nothing.

An app with a hole in it should say where the hole is.

#### Scenario: An unimplemented component is visible

- **WHEN** an app renders a component this platform does not implement
- **THEN** a placeholder is mounted in its place
