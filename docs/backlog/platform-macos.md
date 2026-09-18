# macOS

Part of the [backlog](../BACKLOG.md). Not scheduled.

Six of this file's entries were struck on 2026-09-18 after being checked
against the code rather than remembered. Five of them had been done for days.
If an entry here is about to be picked up, run the thing it describes first.

**Open (9):**

1. Hit testing ignores `transform`
2. Justified text
3. Fonts loaded at runtime are untested
4. No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`
5. Nothing tested against a real screen reader
6. No accessibility subroles
7. `accessibilityValue`, `accessibilityLiveRegion` and `accessibilityLabelledBy`
8. No animated images
9. No gesture cancellation from the platform

- **Hit testing ignores `transform`.** `RnAppKitHitTest` walks `child.frame` and
  never consults the layer transform, so a rotated view is clickable where it
  used to be and not where it is drawn. GTK does not have this bug, because it
  composes the transform into the widget's allocation and `gtk_widget_pick`
  follows -- which is the reason `docs/DECISIONS.md` gives for putting it there
  rather than in the paint. Windows does not have it either, because it inverts
  each view's matrix on the way down. Found in phase 40 while writing the
  Windows equivalent; not fixed, because there is no Mac to check a fix on. The
  test that would catch it is `hit_test_follows_a_transform` in
  `packages/react-native-basalt-win32/native/tests/test_win32_hittest.cpp`.
- ~~**No accessibility.**~~ Done: roles, names, hints, states and hiding, with
  nine tests in `test_appkit_accessibility.mm`. What is left of it is listed
  separately below -- subroles, `accessibilityValue`, and nothing having been
  tried against a real screen reader -- which is a different claim from the one
  this entry made.
- **Justified text.** `NSTextAlignmentJustified` reaches the paragraph style and
  Core Text ignores it for lines drawn individually, which is how RnTextLayout
  has to draw them to honour `numberOfLines`. Doing it properly needs
  `CTLineCreateJustifiedLine` per line.
- **Fonts loaded at runtime are untested.** `resolveFontFamily` is wired into
  the Core Text font lookup, and `expo-font` on macOS has never been run end to
  end.
- ~~**No `maxLength` on macOS.**~~ Done, on both peers:
  `textinput_max_length_is_enforced_on_both_peers`.
- ~~**No multiline `<TextInput>`**~~ Done on both: the NSTextView and
  GtkTextView peers, with the text and the selection round-tripping through
  each -- `textinput_multiline_builds_a_text_view` and its two siblings.
- **No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`** on
  macOS, all of which AppKit has some form of.
- **Nothing tested against a real screen reader**, on either platform.
  VoiceOver and Orca are both a manual step nobody has taken; the unit tests
  assert the properties were set and cannot assert the result is usable.
- **No accessibility subroles.** A search field should be a text field with
  `NSAccessibilitySearchFieldSubrole`; reporting only the role loses the "this
  searches" part.
- **`accessibilityValue`, `accessibilityLiveRegion` and
  `accessibilityLabelledBy`** are unimplemented on both platforms.
- **No animated images.** The first frame of a GIF is drawn as a still, on both
  desktops.
- ~~**No scrollbars.**~~ Done, and on all three -- the claim that the GTK side
  got them from its widget theme was never true: neither host drew one. See the
  ScrollView section for the shape. AppKit's own are `NSScroller`, which comes
  with `NSScrollView` and so was never available here, so the indicator is drawn
  from `core/ScrollIndicator.h` like the other two.
- ~~**No scroll momentum or elasticity.**~~ Done: AppKit reports the phases the
  system's own deceleration goes through, which is what lets
  `onMomentumScrollBegin` and `onMomentumScrollEnd` be answered honestly here.
  See the ScrollView section for what is left, which is Windows.
- ~~**Nothing is reachable by Tab**~~ Done: Tab visits focusable views in tree
  order and wraps, and what counts as focusable is what `accessible` marks --
  `focus_tab_visits_focusable_views_in_tree_order_and_wraps`.
- **No gesture cancellation from the platform.** `dispatchTouchCancel` exists
  and nothing calls it: AppKit has no equivalent of GTK's gesture `cancel`, and
  the case it covers -- a press interrupted by the window losing focus -- has no
  handler yet.
- ~~Within `<View>`: per-corner radii, borders, transform, z-index and
  pointer-events are unmapped.~~ All five are mapped, and have been since
  2026-09-13 -- see "The borders macOS was never drawing". The entry outlived
  the gap by five days, which is the second time this file has claimed
  something missing that was done; the title bar was the first.

  Checked by comparing rather than by reading: `scripts/compare_hosts.sh focus
  BasaltFocus` has GTK and AppKit agreeing on `radii=`, `borderw=` and
  `borderc=`, and `index BasaltDemo` on `transform=`.
