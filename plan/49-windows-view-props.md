# 49. Windows: per-corner radii and per-edge borders

The handoff phase. Windows is now the only host whose `<View>` prop surface is
behind, and the gap is exactly two props that the other two already implement
the same way. This is written to be picked up on a Windows machine, because
neither half of it can be compiled anywhere else.

## What is missing

`Win32MountingManager::applyViewProps` applies one circular radius and no
border at all:

    view->setCornerRadius(borders.borderRadii.topLeft.horizontal);
    // TODO(props): per-corner radii, borders, pointerEvents.

`RnWin32View` stores `float cornerRadius_` and nothing else. GTK has had all
of this since phase 5 and AppKit since phase 47, so what follows is a port
rather than a design.

## The contract the other two implement

Both keep the same shape, and Windows should keep it too:

- **Radii** are four corners, each with a horizontal and a vertical radius --
  React Native's are elliptical -- in the order top-left, top-right,
  bottom-right, bottom-left. Read them off `resolveBorderMetrics`, which has
  already resolved percentages and clamped opposite corners against the frame.
- **Widths and colours** are four each, top, right, bottom, left, the order CSS
  names them. A missing colour is **transparent, not black**: an edge with a
  width and no colour paints nothing. GTK and AppKit both make that call and
  Windows has to agree, or the three dump differently for a view none of them
  draws a border for.
- **A view "has borders"** only when some edge has both a width above zero and
  a colour with alpha above zero. That predicate is what `describeTree` keys
  on, so it has to match or the dumps diverge on the flag rather than on the
  values.

## Painting, in Direct2D

The fill is the easy half. `RnWin32View::paint` currently does

    FillRoundedRectangle(D2D1::RoundedRect(bounds, cornerRadius_, cornerRadius_))

which cannot express four different corners. It needs an `ID2D1PathGeometry`
built from the four radii. `Win32Clip.h` already shows the shape of this --
`GetFactory` off the render target, `CreatePathGeometry`, open a sink -- and
`ScopedGeometryClip` needs the same treatment, since `overflow: hidden` has to
clip to the same outline the background is filled with.

The border is the half with a trap in it, and phase 47 fell into it on AppKit
before this was understood. **Do not clip each edge to a quadrilateral running
from the outer corners in to the inner rectangle.** That tiles the border band
exactly while the corners are square, and leaves a gap the moment a radius
pushes the band outside those quads:

    at y=5 on a 4px border with an 18px radius:
      the band spans x = 5.6 .. 12.8
      a top wedge bounded by y <= 4 does not reach it
      a left wedge bounded by x <= 4 does not reach it
      so that stretch is painted by nothing and stays fill colour

What it looks like is not a missing sliver. The colour stops early and the
corner reads as a **chamfer**, which is how it was reported and which sent the
first investigation after the wrong thing entirely -- the geometry was a
correct arc the whole time.

The fix, and what AppKit does now, is two half-plane clips per edge along the
same diagonals. Successive clips intersect, so the pair is the sector that edge
owns, and a sector has no far end: it covers the arc however large the radius.
It also handles the case a quadrilateral cannot express at all -- when the
border is thick relative to the frame the two diagonals cross inside the view,
which turns a quad into a bowtie and paints the wrong side. In Direct2D that is
`PushLayer` with a geometry mask, or `PushAxisAlignedClip` plus a geometry, per
edge; `RnAppKitView::rnDrawBordersInContext` is the reference.

Then fill "outer minus inner" per edge. AppKit does it as an even-odd fill of
the two subpaths; Direct2D has `CombineWithGeometry` with
`D2D1_COMBINE_MODE_EXCLUDE`, which is more direct.

The inner radii shrink by the width of the edges meeting at that corner --
top-left's horizontal by `left`, its vertical by `top`, and so on -- clamped at
zero, which is what makes a corner whose radius is smaller than its border come
out square on the inside.

## What already exists and does not need doing

`describeTree` on all three hosts prints `radii=` as eight numbers and
`borderw=`/`borderc=` as four each. Windows prints `radii=` today by repeating
its single circular radius eight times, which is an honest description of what
it paints; that goes away with the real four. It prints no `borderw=`/
`borderc=` at all, which is why `scripts/compare_hosts.sh` currently fails
against a Windows host on any bordered view -- deliberately, since phase 47.

That is also the check that says when this phase is done: `compare_all.sh`
against a Linux host, with the GTK host in WSL, should go back to agreeing on
all twelve apps.

## Tests

Windows is the only host that can assert on pixels -- Direct2D renders into a
WIC bitmap with no window, no device and no display -- so this is the one place
the rendering can be pinned rather than inferred. `test_win32_paint.cpp` is
where those go, beside `win32_paint_rounds_the_background_to_the_corner_radius`.

Worth asserting, and none of it is expressible in a tree dump:

- A corner with a radius keeps its border all the way round. Sample the
  outermost painted pixel along the arc and check it is the border colour and
  not the fill -- that is exactly the assertion that would have caught the
  AppKit bug, and neither other host can make it.
- Two edges of different colours mitre on the diagonal rather than overlapping.
- A corner whose radius is smaller than its border width is square inside.
- Per-corner radii differ: a view with only `borderTopLeftRadius` is round in
  one corner and square in the other three.

`test_win32_view.cpp` should also gain the dump assertions, matching
`test_viewprops.cpp` and `test_appkit_input.cpp`.

## Not in this phase

`pointerEvents`, which is missing on all three. It is not a port: GTK hit tests
through `gtk_widget_pick`, and `box-none` and `box-only` cannot be expressed
with `can-target`, so a shared implementation needs a custom pick on that host
first. Worth doing after this, and worth doing on all three at once.
