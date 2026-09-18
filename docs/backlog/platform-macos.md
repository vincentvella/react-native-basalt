# macOS

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (15):**

1. Hit testing ignores `transform`
2. No accessibility
3. Justified text
4. Fonts loaded at runtime are untested
5. No `maxLength` on macOS
6. No multiline `<TextInput>`
7. No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`
8. Nothing tested against a real screen reader
9. No accessibility subroles
10. `accessibilityValue`, `accessibilityLiveRegion` and `accessibilityLabelledBy`
11. No animated images
12. No scroll momentum or elasticity
13. Nothing is reachable by Tab
14. No gesture cancellation from the platform
15. Within <View>: per-corner radii, borders, transform, z-index and pointer-event

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
- **No accessibility.** AppKit views carry no role, so a screen reader sees a
  tree of untyped views. It is also the only thing `scripts/compare_hosts.sh`
  finds different between the two hosts on a text-heavy app: GTK emits
  `role=label` on a paragraph and macOS emits nothing.
- **Justified text.** `NSTextAlignmentJustified` reaches the paragraph style and
  Core Text ignores it for lines drawn individually, which is how RnTextLayout
  has to draw them to honour `numberOfLines`. Doing it properly needs
  `CTLineCreateJustifiedLine` per line.
- **Fonts loaded at runtime are untested.** `resolveFontFamily` is wired into
  the Core Text font lookup, and `expo-font` on macOS has never been run end to
  end.
- **No `maxLength` on macOS.** An NSTextField has no maximum length; enforcing
  one needs a formatter or a delegate that rejects edits. GTK gets it from
  `gtk_text_set_max_length`, so the two behave differently.
- **No multiline `<TextInput>`** on either platform. It wants an NSTextView in a
  scroll view on macOS, and a GtkTextView on GTK -- a different peer either way.
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
- **No scroll momentum or elasticity.** A trackpad flick stops dead, which is
  visibly un-Mac-like. `onMomentumScroll*` never fire, as on GTK.
- **Nothing is reachable by Tab**, on either platform: no tab order is
  implemented, so a field can be clicked into and not tabbed into. A focused
  field does take keyboard input, as of phase 28.
- **No gesture cancellation from the platform.** `dispatchTouchCancel` exists
  and nothing calls it: AppKit has no equivalent of GTK's gesture `cancel`, and
  the case it covers -- a press interrupted by the window losing focus -- has no
  handler yet.
- Within `<View>`: per-corner radii, borders, transform, z-index and
  pointer-events are unmapped. The GTK side has all of them.
