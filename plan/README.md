> Names in these documents were swept when the project was renamed from
> `react-native-linux` to `react-native-basalt` in phase 22, so paths and
> identifiers are current rather than as-written. See plan/22-the-name.md.

# Plan

Working notes for react-native-basalt. Committed so they survive a move between
machines, but still notes rather than documentation: rougher than the README,
and allowed to be wrong or out of date. Anything that becomes true and durable
should graduate into `README.md` or `docs/ARCHITECTURE.md`.

- `02-reacthost-surface.md` — next milestone: a live surface driven by Hermes
- `03-metro-bundle.md` — React actually reconciling, with Fast Refresh
- `04-text-pango.md` — the big one
- `33-dev-bundle-errors.md` — what Metro said, instead of what it compiled to
- `34-expo-dependencies.md` — what a real app's libraries do, and the one that blocked the build
- `35-expo-modules.md` — the registry filled: clipboard, linking, the manifest
- `36-expo-image.md` — an Expo view is a Fabric component with a config in front
- `37-gesture-handler.md` — a library with no portable C++, so the contract was the port
- `38-reanimated.md` — worklets, Reanimated, and a mount React Native never reported
- `backlog.md` — everything not yet scheduled
- `decisions.md` — choices made and why, so they are not re-litigated
