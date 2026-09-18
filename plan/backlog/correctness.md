# Correctness gaps in what exists

Part of the [backlog](../backlog.md). Not scheduled.

**Open (5):**

1. borderStyles — dashed and dotted borders
2. backfaceVisibility — needs the transform's determinant sign at paint time
3. pointScaleFactor — fractional scaling under Wayland
4. transformOrigin is passed through but never exercised; the default centre anch
5. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans

- `borderStyles` — dashed and dotted borders. GTK's border node paints solid
  only, so these need a custom path.
- `backfaceVisibility` — needs the transform's determinant sign at paint time.
- `pointScaleFactor` — fractional scaling under Wayland.
- `transformOrigin` is passed through but never exercised; the default centre
  anchor is.
- 3D transforms have no perspective: `gsk_transform_perspective` exists, and
  `Transform` carries the matrix, but nothing sets it up.
