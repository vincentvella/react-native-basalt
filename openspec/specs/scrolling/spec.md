# scrolling Specification

## Purpose

`<ScrollView>`: the offset, what moves it, and where it comes to rest.

A scroll view is not one feature but several that have to agree -- Yoga letting
the content exceed the viewport, the platform clipping it, the offset moving
children *and* being written back into the shadow tree, and `onScroll` reaching
JavaScript. Three of those are invisible from a screenshot, which is why they
are stated here rather than left to look right.

The offset is the platform's, not a toolkit scroll container's. Adopting one
would mean handing it the scrolling too, and then React Native and the toolkit
would both believe they owned the position.

## Requirements

### Requirement: Scrolling moves the content and tells React

The system SHALL move a scroll view's children by its offset, SHALL write that
offset back into the shadow tree unthrottled -- because `measure`, hit testing
and view culling all read it -- and SHALL emit `onScroll` to JavaScript,
throttled by `scrollEventThrottle`.

The offset SHALL be clamped to the content, so a scroll view never rests outside
what it holds.

#### Scenario: A wheel scrolls the content

- **WHEN** the person scrolls over a scroll view whose content overflows
- **THEN** the children move by the offset
- **AND** `onScroll` reports the new offset to JavaScript

#### Scenario: The offset is readable from React

- **WHEN** a scrolled view is measured from JavaScript
- **THEN** the measurement accounts for the scroll position

### Requirement: A programmatic scroll arrives where it was asked to

The system SHALL move a scroll view to an offset an app asks for through
`scrollTo` or `scrollToEnd`, and SHALL animate that move when `animated` is
set rather than jumping to it.

#### Scenario: A programmatic scroll can be animated

- **WHEN** an app calls `scrollTo` with `animated: true`
- **THEN** the offset moves through intermediate positions rather than jumping
- **AND** it finishes at exactly the offset asked for
- **AND** a gesture during the animation cancels it

### Requirement: A list settles where the props say

The system SHALL settle a scroll view on a snap point when `pagingEnabled`,
`snapToInterval` or `snapToOffsets` is set, choosing the next point in the
direction the gesture was flicked.

#### Scenario: A paging or snapping list settles on a point

- **WHEN** a scroll view sets `pagingEnabled`, `snapToInterval` or
  `snapToOffsets` and a gesture ends
- **THEN** it animates to the next point in the direction it was flicked
- **AND** it does not coast past it
- **AND** it never settles outside the content

### Requirement: An inset changes the range, not the content

The system SHALL apply `contentInset` by extending how far a scroll view may be
scrolled -- before its content as well as past it -- rather than by resizing or
repositioning the content, which is what an inset means and is not what padding
means.

The system SHALL apply `scrollIndicatorInsets` to the indicator's track alone,
leaving the range unchanged, so that a header overlaying a list can move the
bar without moving the list.

#### Scenario: A leading inset can be scrolled into

- **WHEN** a scroll view with a top `contentInset` is scrolled above its content
- **THEN** it rests inside the inset rather than clamping at the content's top

#### Scenario: An indicator inset moves only the bar

- **WHEN** a scroll view sets `scrollIndicatorInsets` and not `contentInset`
- **THEN** the indicator's track starts and ends inside those insets
- **AND** how far the view scrolls is unchanged

### Requirement: A scroll view shows where you are in it

The system SHALL draw an overlay scroll indicator whose geometry is decided once
and shared by all three desktops, rather than borrowed from a toolkit scroll
container that this platform does not use.

#### Scenario: A scroll view shows where you are in it

- **WHEN** a scroll view's content is longer than the view
- **THEN** an overlay indicator is drawn inset from the trailing edge
- **AND** its length is the fraction of the content on screen, above a minimum
- **AND** its position follows the offset, clamped at both ends
- **AND** content that fits draws no indicator at all
- **AND** `showsVerticalScrollIndicator` or `showsHorizontalScrollIndicator` set
  to false removes that axis's indicator without affecting the scrolling

### Requirement: Pulling a list refreshes it

The system SHALL run a scroll view's refresh handler when the person pulls it
past its top, once per gesture, and SHALL re-arm when the list moves again.

#### Scenario: Pulling a scroll view refreshes it

- **WHEN** the person pulls a scroll view carrying a refresh control
- **THEN** the refresh handler runs once for that gesture
