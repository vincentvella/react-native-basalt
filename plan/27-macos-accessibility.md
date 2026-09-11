# Phase 27 — accessibility on macOS, and one vocabulary

> **Done, 2026-09-11.** The two hosts now agree on all six apps.
> `scripts/compare_all.sh` is the sentence that says so.

Phase 26 ended with accessibility as the *only* difference the cross-platform
diff could find, on both the text app and the image app. That made it the
milestone with a finish line, which is a rare and good thing to have.

## The interesting problem was not AppKit

Mapping React Native's roles onto `NSAccessibility` is a table. The interesting
problem was that **the two platforms were describing the same fact in two
languages.**

`describeTree` on GTK reported GTK's own role nicks -- `label`, `img`, `list`
-- read back off the widget. The AppKit side would naturally report
`AXStaticText`, `AXImage`, `AXList`. Both are correct, and a line-by-line diff
of the two would show every accessible view as a difference forever.

So the dump now reports **React Native's** name on both: `text`, `image`,
`list`. That is the vocabulary the app wrote, the toolkit role is an
implementation detail, and this dump exists to be compared.

The thing given up is real and worth naming. GTK's version read the role back
out of the widget, which proved it had actually been applied; reporting a stored
string proves only that we were told. So the proof moved rather than vanished:
`tests/test_accessibility.cpp` already asserted the real `GtkAccessibleRole` for
each React Native role, and `tests/test_appkit_accessibility.mm` now asserts the
real `NSAccessibilityRole`. A platform question is asserted per platform; the
portable name is what crosses between them.

On GTK the name is set at construction, beside the `GtkAccessibleRole`, rather
than in `applyAccessibility`. GTK's role is construct-only, so a role changed
after mount would leave the name reporting something the widget is not.

## What AppKit got

Roles, labels, hints, states and hiding, through `NSView`'s own accessibility
properties rather than by overriding the protocol -- they have been settable
since 10.10, and setting them is both less code and less to get wrong.

Four decisions worth recording.

**A plain `<View>` is not an accessibility element.** Leaving every one in the
tree would bury the handful that mean something under hundreds that do not. A
role puts it back, and so does a label on its own, which is the common case for
an icon-only `<Pressable>`.

**An unrecognised role is a group, not a guess.** A wrong role is worse for a
screen reader than a vague one, because it makes a control announce itself as
something it is not. Several entries in the table are the closest thing rather
than an equivalent, and each says so: macOS has no toggle-button or switch role
(VoiceOver announces both as checkboxes, which is what `NSSwitch` itself
reports), a tab is a radio button in a tab group, a search field is a text field
with a subrole this does not set yet, and `header` and `alert` have no view-level
equivalent at all.

**Unset is not false.** Each state is a tri-state, because "the app did not
mention `disabled`" and "the app said it is enabled" are different claims and
only one of them was made.

**`busy` is unreported.** React Native means "this is loading", which VoiceOver
has no way to be told about a plain view; the nearest thing is a progress
indicator, which is a different role rather than a state. Left out rather than
mapped onto something that means something else.

## Where the two hosts stand

```
  views    identical, frames included
  press    identical, frames included
  scroll   identical, frames included
  image    identical, frames included
  text     identical, frames ignored
  a11y     identical, frames ignored

  the two hosts agree on all 6 apps
```

Four of six match exactly, frames and all. The two that need frames ignored are
the two whose layout depends on text measurement, where Pango over the system
sans and Core Text over San Francisco cannot agree and never will -- what is
compared there is the tree shape, the strings, the colours, the roles and the
flags, which is everything that is not a font metric.

That is as close to "the same app" as two different rendering stacks can get,
and it is checked by a script rather than asserted in a README.

## What is missing

**Nothing has been tested against a real screen reader.** VoiceOver and Orca are
both a manual step nobody has taken. The unit tests assert that the properties
were set; they cannot assert that the result is usable, and there is a real
distance between those.

**No accessibility actions.** `IMountingManager` declares
`dispatchAccessibilityAction` and neither platform implements it, so
`accessibilityActions` and `onAccessibilityAction` do nothing. The same gap on
both, listed in the backlog since phase 07.

**No keyboard focus model**, so nothing is reachable by Tab on either platform.
That arrives with `<TextInput>`.

**`accessibilityValue`, `accessibilityLiveRegion` and `accessibilityLabelledBy`**
are unimplemented on both. macOS uses `accessibilityValue` for `checked`, which
means the prop of the same name has nowhere to go yet.

**No subroles.** A search field should be a text field with
`NSAccessibilitySearchFieldSubrole`, and reporting only the role loses the
"this searches" part.
