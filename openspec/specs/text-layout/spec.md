# text-layout Specification

## Purpose

`<Text>`: measured by the platform's own text engine, and drawn at the size it
was measured at.

Text is the one component where the platform, not Yoga, decides the size. Fabric
asks a `TextLayoutManager` how big a paragraph is and lays out around the answer,
so measurement and drawing going through different code is the failure to avoid
-- a paragraph measured one way and drawn another is clipped or floats in space,
and looks like a layout bug rather than a text one.

## Requirements

### Requirement: A paragraph is measured by the platform

The system SHALL measure text with the desktop's own text engine -- Pango, Core
Text, DirectWrite -- and SHALL draw it through the same layout object that was
measured, so the box Yoga was given is the box the glyphs land in.

Measured sizes SHALL respond to the text's own attributes: a larger font
measures larger, bold measures wider than regular, and empty text measures to
zero width.

#### Scenario: A paragraph reports a size

- **WHEN** a `<Text>` is measured
- **THEN** it reports a width and a height greater than zero

#### Scenario: Size follows the font

- **WHEN** the same string is measured at a larger `fontSize`
- **THEN** it measures larger

#### Scenario: Font size is absolute

- **WHEN** a `fontSize` is given
- **THEN** it is treated as an absolute size rather than as points scaled by the
  desktop's font settings, so the three hosts agree

### Requirement: Text wraps and truncates as React Native asks

The system SHALL wrap a paragraph to the width it is constrained to, growing
taller, and SHALL honour `numberOfLines` by limiting the height and truncating
the text that does not fit.

A `numberOfLines` the text already fits within SHALL NOT truncate it, and the
default ellipsize mode SHALL NOT collapse a paragraph that was never
constrained.

#### Scenario: Constraining the width wraps

- **WHEN** a paragraph is measured against a width narrower than one line
- **THEN** it wraps, and the measured height grows

#### Scenario: A line limit truncates

- **WHEN** `numberOfLines` is fewer lines than the text needs
- **THEN** the measured height is limited to that many lines

#### Scenario: A line limit that fits changes nothing

- **WHEN** `numberOfLines` is at least the number of lines the text needs
- **THEN** the text is not truncated

### Requirement: Alignment moves the glyphs and not the box

The system SHALL apply `textAlign` when drawing without changing the measured
size, so that alignment cannot move a paragraph's layout out from under Yoga.

#### Scenario: Alignment does not change measurement

- **WHEN** the same paragraph is measured with different `textAlign` values
- **THEN** every measurement is the same

#### Scenario: Alignment changes where the ink lands

- **WHEN** a paragraph is drawn with a non-default `textAlign`
- **THEN** the glyphs are drawn at the aligned position within the box
