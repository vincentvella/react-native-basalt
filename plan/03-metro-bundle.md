# Phase 3 — Metro, flexbox, Fast Refresh

**Goal:** `npx react-native start` serving a bundle that this host loads, with
Fast Refresh working. First point where React is genuinely reconciling.

## Work

- **Platform registration.** Metro needs to resolve `.linux.tsx` / `.linux.ts`.
  A `metro.config.js` wrap adding `linux` to `resolver.platforms`.
- **Dev server.** `ReactCxxPlatform/react/devsupport` already has
  `DevServerHelper` and `PackagerConnection`. Point `ReactInstanceConfig` at
  `localhost:8081` instead of a bundle path.
- **Fast Refresh.** Comes from the packager connection + `ReactHost`'s reload
  path (`reloadReactInstance`). Mostly a matter of wiring the reload signal.
- **LogBox.** `ReactHost` takes a `logBoxSurfaceDelegate`. A second surface in
  its own GTK window is probably the cheapest real implementation.
- **Flexbox verification.** Yoga is already in the build and platform-agnostic,
  so this should mostly work once real props flow. Verify against a reference
  screenshot from iOS/Android for the same JSX.

## Risks

- The RN CLI assumes iOS/Android project layouts. Getting `run-linux`
  registered as a CLI platform command is a separate chunk (phase 7); for this
  phase, launching the binary by hand is fine.
