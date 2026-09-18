# context-menu Specification

## Purpose

The menu that pops up where you press. Every desktop has one, including the one
with no menu bar, which makes it the more portable of the two kinds.

## Requirements

### Requirement: An app can show a context menu at a point

The system SHALL show a native popup menu from a flat list of items, separators
and disabled items, positioned at a point in the window's coordinates, or at the
pointer when no point is given.

The menu SHALL be one level. A popup on each of these desktops is a list; the
nesting an application menu has is what a menu bar is for.

#### Scenario: A menu opens and reports the item chosen

- **WHEN** an app shows a menu and the person chooses the third entry
- **THEN** the call answers with that entry's index
- **AND** indexes count separators, so they line up with the list passed in

#### Scenario: A dismissed menu is told apart from a chosen one

- **WHEN** the menu is dismissed without a choice
- **THEN** the call answers with null rather than an index

#### Scenario: The chosen item's handler runs

- **WHEN** an entry carrying a handler is chosen
- **THEN** that handler runs before the call answers

### Requirement: A secondary click asks for a context menu

The system SHALL deliver a secondary or middle click as a pointer event carrying
which button it was, and SHALL NOT deliver it as a press.

A secondary click is not an activation on any desktop: it must not reach the
responder system, a gesture handler, or `onPress`. This is what lets one view be
both a button and a context-menu target.

#### Scenario: A right-click reports its button

- **WHEN** the person right-clicks a view
- **THEN** the view's pointer-down handler runs with the secondary button number

#### Scenario: A right-click does not press what it lands on

- **WHEN** the person right-clicks a pressable view
- **THEN** that view's press handler does not run
