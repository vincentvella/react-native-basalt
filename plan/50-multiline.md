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
