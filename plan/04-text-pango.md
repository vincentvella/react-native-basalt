# Phase 4 — Pango TextLayoutManager

The largest single remaining piece.

## The gap

`textlayoutmanager/platform/cxx/.../TextLayoutManager.cpp` is a stub. It
ignores `ParagraphAttributes` entirely and returns
`layoutConstraints.minimumSize`. Nothing measures text.

## Shape of the work

Implement `TextLayoutManager::measure` (and the lines/attachment variants)
against Pango:

- `AttributedString` fragments → `PangoAttrList` (font family, size, weight,
  style, colour, letter spacing, line height, decoration).
- `ParagraphAttributes` → `PangoLayout` config: `maximumNumberOfLines`,
  `ellipsizeMode`, `textBreakStrategy`, `adjustsFontSizeToFit`.
- `LayoutConstraints` → `pango_layout_set_width` (in Pango units — multiply by
  `PANGO_SCALE`).
- Attachments (inline views) → `PangoAttrShape` placeholders, returning their
  rects in `TextMeasurement::Attachments`.

## Correctness traps

- **Pango units.** Everything is 1/1024 px. Mixing raw px in is the classic bug.
- **Font fallback** differs from CoreText/Android; expect metric divergence.
- **`fontSizeMultiplier`** from `LayoutMetrics` must be applied.
- **Measurement caching.** `TextLayoutManager` already holds a
  `textMeasureCache_`; use it, since Yoga measures repeatedly during layout.
- **Baseline** for `alignItems: baseline` comes from `pango_layout_get_baseline`.

## Then

Register `ParagraphComponentDescriptor`, `TextComponentDescriptor` and
`RawTextComponentDescriptor` in `LinuxComponentRegistry.h`, and give
`<Paragraph>` a GTK peer that renders the `PangoLayout` in `snapshot`.
