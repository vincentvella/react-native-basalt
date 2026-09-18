# Correctness gaps in what exists

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (4):**

1. borderStyles — dashed and dotted borders
2. pointScaleFactor — fractional scaling under Wayland
3. transformOrigin is passed through but never exercised; the default centre anch
4. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans

- `borderStyles` — dashed and dotted borders. GTK's border node paints solid
  only, so these need a custom path.
- `pointScaleFactor` — fractional scaling under Wayland.
- `transformOrigin` is passed through but never exercised; the default centre
  anchor is.
- 3D transforms have no perspective: `gsk_transform_perspective` exists, and
  `Transform` carries the matrix, but nothing sets it up.
