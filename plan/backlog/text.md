# Text

Part of the [backlog](../backlog.md). Not scheduled.

**Open (7):**

1. Inline views (<Text><View/></Text>) measure as zero-sized attachments
2. No baseline, so alignItems: 'baseline' is wrong for text
3. numberOfLines with ellipsizeMode: 'clip' does not truncate
4. Ignored: adjustsFontSizeToFit, textBreakStrategy, hyphenation, textShadow*, te
5. One PangoLayout is rebuilt per Paragraph per mutation, including layout-only u
6. All measurement serialises on one mutex; see plan/decisions
7. Text is not selectable and reports nothing to AT-SPI

- Inline views (`<Text><View/></Text>`) measure as zero-sized attachments.
  Doing it properly means `PangoAttrShape` placeholders sized from the child's
  own measurement, and returning their rects from `measure`.
- No baseline, so `alignItems: 'baseline'` is wrong for text.
  `pango_layout_get_baseline` is the value; plumbing it needs
  `TextLayoutManagerExtended`.
- `numberOfLines` with `ellipsizeMode: 'clip'` does not truncate. Pango only
  honours a line limit when ellipsizing, so clip needs a clip node in the widget.
- Ignored: `adjustsFontSizeToFit`, `textBreakStrategy`, hyphenation,
  `textShadow*`, `textTransform`, `fontVariant`, `fontVariationSettings`.
- One PangoLayout is rebuilt per Paragraph per mutation, including
  layout-only updates that did not change the text.
- All measurement serialises on one mutex; see `plan/decisions.md`.
- Text is not selectable and reports nothing to AT-SPI.
