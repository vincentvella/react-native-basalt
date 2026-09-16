# 50. `multiline`, which is only the peer

The last `<TextInput>` gap, and smaller than it looks in one way and larger in
another. Measured before starting, because the obvious assumption -- that
multiline needs new components, new props plumbing and new measurement -- turns
out to be wrong on all three counts.

## What already works

**Nothing is needed in JavaScript.** `RCTMultilineTextInputNativeComponent` and
`RCTSingelineTextInputNativeComponent` differ by exactly one line:

    uiViewClassName: 'RCTMultilineTextInputView'
    uiViewClassName: 'RCTSinglelineTextInputView'

They share `RCTTextInputViewConfig`, which already declares `multiline` as a
prop, and `componentNameByReactViewName.cpp` rewrites *both* names to
`TextInput`. So the component this project already renders carries the prop,
and the descriptor already registered handles it. `multiline` reaches props
today through `...rest` with nothing added.

**Measurement already works.** `BaseTextInputShadowNode::getTextConstraints`
branches on it: a multiline input measures against the real constraints so the
text wraps, and a single-line one measures against infinite width because it is
a horizontal scroller. That runs through whichever text layout manager the
platform installed -- Pango, Core Text, DirectWrite -- all of which this
project supplies.

Measured, on a 200pt-wide field holding a sentence that wraps:

    single line   200x36      (both hosts)
    multiline     200x45      GTK
    multiline     200x51      AppKit

The heights differ because the shapers and fonts do; that is the same reason
`compare_all.sh` ignores frames for the text-driven apps.

## What is missing

Only the peer. The measured box is the right size and the text inside it is
still in a single-line widget, so it renders on one line in a box tall enough
for three.

And the peer is not a property to flip. On both hosts the multiline widget is a
different class with a different text API:

| | single line | multiline |
|---|---|---|
| GTK | `GtkText`, which is a `GtkEditable` | `GtkTextView`, which is **not** -- it has a `GtkTextBuffer` |
| AppKit | `NSTextField` | `NSTextView`, normally inside an `NSScrollView` |

`GtkEditable` is where `gtk_editable_get_text`, `set_position`,
`select_region` and `get_selection_bounds` live, and `GtkTextView` implements
none of them. Every one of those calls is in `GtkTextInputManager` today, and
`RnView` stores the peer as a `GtkText *` -- in the struct, in
`rn_view_set_editable`, in `rn_view_get_editable`, and in the tests.

## The shape of the work

A seam, not a branch at every call site. Both managers do the same six things
to a peer and nothing else:

    text()            setText(value)
    selection()       setSelection(start, end)
    focus()           insertionPointFromClick

Putting those behind a small per-peer interface is the whole refactor, and it
is what makes the rest fall out: the controlled loop, the staleness rule, the
event emission and the styling are all written against those six and do not
care which widget is underneath.

Do GTK first. It is the target platform, its tests run headlessly, and
`test_textinput.cpp` already covers the contract that must not regress -- the
controlled loop, the caret surviving a prop mid-word, a stale command being
dropped, the peer sitting inside the content inset.

## Watch for

- **The peer is the thing focused.** Focus, key handling and the
  `onKeyPress` capture-phase controller are all attached to the peer widget in
  `update()` when it is first created. A second peer class means that wiring
  has to happen for both, and it is easy to attach to one and test the other.
- **`describeTree` prints `editable="..."`**, which is how the end-to-end suite
  and `compare_all.sh` see a field's contents at all. Whatever the multiline
  peer is, it has to report through the same field or twelve apps stop
  agreeing.
- **The single-line peer must keep behaving exactly as it does.** `multiline`
  defaults to false, so every existing field takes the old path; a refactor
  that changes single-line behaviour has broken far more than it fixed.
- **AppKit's `NSTextView` wants a scroll view** for anything longer than its
  box. `<ScrollView>` on this platform is already the host's own, so nesting
  AppKit's is a decision rather than a default -- and iOS's multiline input is
  itself a scroll view, which is why the metrics carry `contentOffset` and
  `contentSize` that both hosts currently answer with the container size.


## Done on GTK, phase 54

The seam is `gtk/GtkTextPeer.h`: eleven functions, C rather than C++ because
`RnView.cpp` uses it too and that file knows nothing about React Native. Every
`GtkEditable` call in `GtkTextInput.cpp` now goes through it, and `RnView`
holds a `GtkWidget *` rather than a `GtkText *`.

Four things were not in the plan and are worth having written down.

- **A `GtkTextView` paints its own background**, from the GTK theme, straight
  over the one `RnView` drew from props. A bare `GtkText` does not, which is
  why the single-line path never needed a stylesheet and this is the first CSS
  provider in the project. Without it a styled field is a white box with
  invisible white text in it.
- **A `GtkTextTag` has no `attributes` property.** The single-line path hands
  `GtkText` a `PangoAttrList` directly; the buffer equivalent is a tag, and a
  tag takes `font-desc` and `foreground-rgba` instead -- so the two are pulled
  back out of the list.
- **Switching `multiline` replaces the widget**, and the text has to be carried
  across by hand. The controlled loop will not put it back: from its side the
  `text` prop did not change, so there is nothing to apply. AppKit already does
  exactly this when `secureTextEntry` rebuilds its field.
- **`activate` is a single-line signal**, and Enter in a multiline field
  inserts a newline rather than submitting -- which is what
  `SubmitBehavior::Newline` already says and what `BaseTextInputProps` already
  reports.

What a multiline field does not have: a placeholder, which `GtkTextView` has no
notion of and which would mean drawing the text; and `maxLength`, which
`GtkTextBuffer` has no equivalent for and which would fight the controlled loop
if enforced by hand. Both are in `plan/backlog.md`.

## Done on AppKit, phase 55

`appkit/AppKitTextPeer.h`, the same shape as the GTK one and in the *view*
target for the same reason: `RnAppKitView` reads the peer's text for its tree
dump, and that target has to keep linking on its own.

Offsets across this seam are UTF-16 units rather than characters, because that
is what `NSRange` means and what JavaScript means by a string index. The GTK
seam says characters for the same reason -- each platform's native unit is the
one its string type uses -- and that asymmetry is deliberate rather than an
oversight.

No `NSScrollView`. An `NSTextView` normally lives in one, and the note above
said that was a decision: the decision is no, because `<ScrollView>` here is
already the host's own and nesting AppKit's would give a multiline field
scrolling that no other component on this platform has. It matches the GTK
side, which is a bare `GtkTextView` and not a `GtkScrolledWindow`.

Two things fell out that were not about multiline at all.

- **A single-line `NSTextField` wraps.** It has done all along; it was
  invisible because no field in the demo or the tests held more text than it
  could show. React Native's single-line input scrolls horizontally instead, so
  the peer now sets `usesSingleLineMode` and clips. Found by putting a long
  string in a field next to a multiline one and looking at both.
- **An `NSTextView` is not an `NSControl`**, so it posts `NSText`'s
  notifications through `NSTextViewDelegate` rather than `NSControl`'s. Same
  two moments, different names: `textDidChange:` and `textDidEndEditing:`
  beside the `controlText...` pair.

`maxLength` is no longer among the gaps -- phase 56 closed it, and found it was
wider than multiline: AppKit had never enforced it on *either* peer, with a
comment saying so. Neither platform offers a property for it on the multiline
peer, so both refuse the edit that would cross the limit -- a GtkTextBuffer
through `insert-text`, an NSTextView through its delegate -- and the
single-line AppKit field takes an NSFormatter, which is what AppKit offers
instead. Refused whole rather than truncated, so a paste that would overflow
leaves what was there, which is what GtkText already did with its own limit.

The placeholder is done too, in phase 57, and it is drawn rather than set --
neither `GtkTextView` nor `NSTextView` has one. `NSTextView` already had a
subclass to hang it on; `GtkTextView` got one, `RnTextView`, whose `snapshot`
chains up and then draws the text when the buffer is empty. A subclass rather
than an overlay on either, because the peer is one widget and `RnView`'s
allocation places exactly one.

Its colour is the field's own foreground at 45% alpha rather than a fixed
grey, so a placeholder stays legible against whatever background the app
chose. And both redraw when the buffer goes empty-to-not and back, which
neither toolkit has any reason to do by itself.

So a multiline field now has everything the single-line one does.
