# Phase 23 — text on macOS, over Core Text

> **Done, 2026-09-11.** `<Text>` measures, wraps, truncates and draws on AppKit.
> With frames ignored, the two hosts' trees differ by exactly one thing.

This fills the second of the three seams phase 19 found. It is also the single
biggest gap between "macOS renders coloured boxes" and "macOS runs an Expo app":
almost every real screen has text on it, and until now registering
`ParagraphComponentDescriptor` on macOS was a *link error* rather than a missing
feature.

## The seam, restated

React Native's cxx platform declares `TextLayoutManager` and implements it as a
stub that ignores every attribute and returns `layoutConstraints.minimumSize`.
`cmake/ReactNativeCore.cmake` drops that stub from the build so a platform's own
can be the only definition. So a desktop platform either writes one or cannot
link anything that constructs one -- and `ParagraphComponentDescriptor`
constructs one, which is why `mac/ComponentRegistryAppKit.mm` had exactly one
entry.

`appkit/CoreTextLayoutManager.mm` is that implementation, and it is
deliberately the same file as `gtk/PangoTextLayoutManager.cpp` with a different
engine inside -- same cache, same font-generation invalidation, same clamping,
same attachment handling, same version guards.

## What differs between the two engines

Three things, and they are worth separating from the incidental spelling.

**Width is not part of the layout.** A `PangoLayout` is built against a width
and rebuilt when it changes; a `CTFramesetter` is not, so width is an argument
to measuring and drawing instead. `buildTextLayout` therefore takes no width on
this side, which is a nicer shape and the only place the two APIs differ in
structure rather than in naming.

**Core Text has no line limit.** Pango takes `numberOfLines` directly, as a
negative height. Core Text breaks as many lines as the text needs and a frame
holds as many as fit in its path, so `RnTextLayout` applies the limit itself: it
asks for an unbounded frame, keeps the first N lines, and truncates the last one
*kept*. Truncating the last line that exists instead would put the ellipsis
after text that already fit and make nothing look omitted.

The truncated line is also rebuilt from everything that would have followed,
not from that line alone -- `CTLineCreateTruncatedLine` over just the visible
line has nothing to elide and returns it unchanged.

**Alignment lives in the frame, not the line.** Because the lines are drawn one
at a time to honour the limit, the frame's alignment never applies, and every
paragraph drew flush left with `textAlign` silently doing nothing. The fix is
`CTLineGetPenOffsetForFlush` per line, with the flush factor read off the first
fragment's paragraph style -- the same place the Pango side reads alignment
from, because React Native resolves it onto every fragment from the `<Text>`
that owns them.

That one was found by looking at a screenshot, not by reading the code. The tree
dump showed `text="Centred"` at the right size in the right box, and the size
was right, and it was left-aligned.

Nothing here needs a mutex. Core Text is documented as thread-safe; Pango's font
map is not, which is why that file serialises every call and this one does not.

## Where it had to be split

`RnTextLayout` -- the paragraph object -- is in its own file with no React
Native in it, and `CoreTextLayout` builds one out of an `AttributedString`. That
split is not tidiness: `basalt_appkit_view` builds and is tested on a Mac with
nothing else installed, and the view has to be able to *draw* a paragraph
without gaining a React Native dependency to do it. The first attempt put both
in one header and broke that immediately.

Drawing needed one more thing. These views are layer-backed and had never drawn
anything -- every prop was a CALayer property. A layer-backed view with a
`drawRect:` gets its layer contents from that draw and only redraws when told,
so `setRnTextLayout:` sets `needsDisplay`. Without it the first paragraph
appears and no later one ever does.

## What it gets to

`js/text.js` is a React app that is mostly `<Text>`: sizes, weights, colours,
alignment, line height, letter spacing, underline, strikethrough, nested
fragments with their own styles, and `numberOfLines={2}` with an ellipsis. All
of it renders.

Run through `scripts/compare_hosts.sh` with `BASALT_COMPARE_IGNORE_FRAMES=1`,
the two hosts' trees differ by **exactly one thing**: GTK emits `role=label` on
every paragraph and macOS emits nothing, because macOS has no accessibility yet.
Every string, every colour, every clip and opacity flag matches.

Frames have to be ignored for text, and that is not a fudge to be embarrassed
about: Pango over the system sans and Core Text over San Francisco are different
shapers over different fonts, so the same paragraph is a few points taller on
one than the other and everything below it shifts. A 28pt heading is 29 high on
Linux and 33 on macOS. Demanding equality would mean the check could never be
turned on for text at all; ignoring frames keeps everything that *must* match
under test.

## What is missing

**Accessibility**, which the diff above named. AppKit views carry no role, so a
screen reader sees a tree of untyped views. The GTK side infers a role from the
component and the props and has done since phase 07.

**Justified text.** `NSTextAlignmentJustified` reaches the paragraph style and
Core Text ignores it for lines drawn individually; doing it properly needs
`CTLineCreateJustifiedLine` per line.

**Inline views.** `<Text><View/></Text>` arrives as attachment fragments and
gets a zero frame each, exactly as on GTK. Reporting the count keeps
`ParagraphShadowNode` happy; positioning them is a separate piece of work on
both platforms.

**`<TextInput>`.** `buildTextAttributes` exists and has no caller yet -- it is
what a text field will need to honour `color`, `fontSize` and `fontFamily` from
its style. That is the next component, and it needs an `NSTextField` peer and
the controlled-value loop before the attributes matter.

**Fonts loaded at runtime are untested here.** `resolveFontFamily` is wired
through `fontFor`, and `expo-font` on macOS has never been run end to end.
