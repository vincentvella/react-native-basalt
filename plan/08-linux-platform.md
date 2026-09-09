# Phase 8 — The `linux` Metro platform

> **Done, 2026-09-09.** `packages/react-native-linux/`.

**Goal:** an app bundled with `--platform linux` that sees
`Platform.OS === 'linux'`, in development and in a release build alike.

## What it took

Almost nothing of our own: one real `Platform` module, one no-op stub, and a
Metro config that redirects nine of React Native's self-importing shims to
their `.android.js` siblings. The work was in finding out *which* files and
*why*; see `plan/decisions.md`.

The failures are worth knowing because none of them says what is wrong. A shim
that resolves to itself exports undefined, so the error surfaces wherever
something first reads it -- `Platform.constants`, then a view config, then a
component -- each one an afternoon away from the cause if you take them one at
a time. One grep for the note those files carry finds all of them at once.

## What it unblocks

- `.linux.js` resolution, for app code and third-party packages.
- `<TextInput>`, which needs a component name this platform defines. It still
  needs keyboard input and a focus model, which is phase 9.

## Not done

- No `run-linux` CLI, so the host is still launched by hand.
- No npm publishing: the package is `private` and consumed by relative path.
- Nothing verifies the shim list still matches React Native. A new shim upstream
  appears as an undefined export at runtime, which is exactly the failure mode
  this phase existed to stop.
