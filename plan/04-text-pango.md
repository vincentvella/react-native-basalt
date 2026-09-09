# Phase 4 — Pango TextLayoutManager

> **Done, 2026-09-09.** `src/PangoTextLayout.*`,
> `src/PangoTextLayoutManager.cpp`, the Paragraph path in `GtkMountingManager`,
> and the text descriptors in `LinuxComponentRegistry.h`. What the plan got
> right and wrong is at the bottom.

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

## What actually happened

The plan's list of correctness traps was accurate, and two of them bit:

- **Pango units** and **`fontSizeMultiplier`** were handled as described. But the
  units trap has a second half the plan did not name: `set_size` treats its
  argument as *points* resolved against the context dpi, so a `fontSize` of 16
  came out at about 21px. `set_absolute_size` is the right call. See
  `plan/decisions.md`.
- **Measurement caching** was already there and is used.
- **Baseline** is still not plumbed; it needs `TextLayoutManagerExtended`.
- **Attachments** are still zero-sized. See `plan/backlog.md`.

Two things the plan did not anticipate:

- **The default ellipsize mode collapses paragraphs.** React Native defaults
  `ellipsizeMode` to `Tail`, and Pango ellipsizes to one line when no height is
  set, so translating the default faithfully turned every wrapping paragraph
  into a single line. Ellipsization is only applied when `numberOfLines` is.
- **Pango's default font is a serif face**, which is not what React Native means
  by an unset `fontFamily`. The default is now `Sans`.

The registry work at the end of the plan was three descriptors, not one:
`<Text>` becomes a Text node, its string a RawText node, and the outermost
`<Text>` a Paragraph that folds the subtree into one `AttributedString`. Only
Paragraph mounts, which is why `hasComponent` names only it.
