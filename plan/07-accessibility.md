# Phase 7 — Accessibility, and a test suite

> **Done, 2026-09-09.** `tests/`, `scripts/integration_test.py`,
> `docs/TESTING.md`, and the accessibility mapping in `RnView` and
> `GtkMountingManager`.

Phase 7 was meant to be AT-SPI accessibility and `<TextInput>`. It became
accessibility and a test suite, because `<TextInput>` turned out to be blocked
on something else entirely.

## The test suite came first

The project had no tests. Every claim in the README rested on a screenshot,
which is fine for "does this look right" and useless for "did this break".
`build/rn_tests` now covers everything reachable without a JavaScript runtime,
and `scripts/integration_test.py` covers the whole stack by asserting on a
widget tree the host dumps. See `docs/TESTING.md`.

Building it paid for itself immediately: the dump showed that the widget tree
is far flatter than the JSX, which is React Native's view flattening and not a
bug, but which nobody could have noticed from a screenshot. It is now written
down in `docs/ARCHITECTURE.md`.

## Accessibility

`accessibilityRole`, `accessibilityLabel`, `accessibilityHint`,
`accessibilityState` and `accessibilityElementsHidden` map onto GTK's
`GtkAccessible`, which is what AT-SPI and therefore Orca read. GTK ships
assertion helpers for this (`gtktestatcontext.h`), so the tests read back what a
screen reader would be told rather than inferring it.

The one real constraint is that a role is construct-only; see
`plan/decisions.md`.

## What is not done

- **`<TextInput>`**, and the reason is worth reading: `plan/decisions.md`.
- **Accessible actions.** `IMountingManager` declares `accessibleClickAction`,
  `setAccessibilityFocusedView` and friends, and none are implemented, so a
  screen reader can read the interface but not drive it.
- **Focus.** There is no keyboard focus model, so nothing is reachable by Tab.
- **Live regions and relations.** `accessibilityLiveRegion` and
  `accessibilityLabelledBy` are ignored.
- **Nothing has been tested against a real screen reader**, only against GTK's
  own assertions. Orca on the Linux box is the check that matters.
