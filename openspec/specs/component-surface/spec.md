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

#### Scenario: The three desktops agree

- **WHEN** the same app is rendered on two hosts
- **THEN** the resulting view trees match, frames included

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

#### Scenario: Pulling a scroll view refreshes it

- **WHEN** the person pulls a scroll view carrying a refresh control
- **THEN** the refresh handler runs once for that gesture

### Requirement: A component with no implementation renders visibly

The system SHALL render a placeholder for a component it does not implement,
rather than nothing.

An app with a hole in it should say where the hole is.

#### Scenario: An unimplemented component is visible

- **WHEN** an app renders a component this platform does not implement
- **THEN** a placeholder is mounted in its place
