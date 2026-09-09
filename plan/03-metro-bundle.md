# Phase 3 — Metro, flexbox, Fast Refresh

> **Done, 2026-09-09.** `js/index.js`, `js/metro.config.js`, `scripts/bundle.sh`,
> `scripts/metro.sh`, and the dev-mode wiring in `src/main.cpp`. What the plan
> assumed and what actually happened is recorded at the bottom.

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

## What actually happened

- **Platform registration was the wrong idea.** The plan wanted `linux` added to
  `resolver.platforms` so `.linux.tsx` would resolve. Bundles are built for
  `android` instead, because that is what `ReactCxxPlatform` reports to JS and
  what `DevServerHelper` hardcodes into the bundle URL. See
  `plan/decisions.md`.
- **The dev server needed the networking seam first.** `enableDevMode` alone
  hangs with a stub http client, because the bundle download blocks on a future
  the callbacks complete. libcurl for http, and React Native's own uncompiled
  websocket client for the packager connection.
- **`DevSettings` is what forces dev mode.** A `__DEV__` bundle calls
  `TurboModuleRegistry.getEnforcing('DevSettings')` and dies without it, and
  `ReactCxxTurboModuleProvider` only serves that module when a `DevServerHelper`
  exists. So a dev bundle cannot be loaded from disk with dev mode off; it is
  dev-mode-and-Metro, or a production bundle.
- **`loadScript`'s second argument is not decoration.** It is the Metro entry
  path, and `DevServerHelper::getBundleUrl` returns an empty string without it,
  which silently falls back to the on-disk bundle.
- **Flexbox needed no work at all**, as predicted.
- **LogBox is still not done.** See `plan/backlog.md`.
