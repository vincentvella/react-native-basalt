# Accessibility

Part of the [backlog](../backlog.md). Not scheduled.

**Open (5):**

1. Not tested against a real screen reader
2. Accessible actions are unimplemented: IMountingManager declares accessibleClic
3. accessibilityRole cannot change after mount; see plan/decisions
4. accessibilityLiveRegion, accessibilityLabelledBy, accessibilityValue and acces
5. No keyboard focus model, so nothing is reachable by Tab

- Not tested against a real screen reader. GTK's assertions say the properties
  are set; Orca on the Linux box is the check that matters.
- Accessible actions are unimplemented: `IMountingManager` declares
  `accessibleClickAction`, `setAccessibilityFocusedView`,
  `accessibleScrollInDirection` and `accessibleSetText`, and all are no-ops, so
  the interface can be read but not driven.
- `accessibilityRole` cannot change after mount; see `plan/decisions.md`.
- `accessibilityLiveRegion`, `accessibilityLabelledBy`, `accessibilityValue`
  and `accessibilityActions` are ignored.
- No keyboard focus model, so nothing is reachable by Tab.
