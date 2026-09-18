> Names in these documents were swept when the project was renamed from
> `react-native-linux` to `react-native-basalt` in phase 22, so paths and
> identifiers are current rather than as-written. See plan/22-the-name.md.

# Plan

Working notes for react-native-basalt. Committed so they survive a move between
machines, but still notes rather than documentation: rougher than the README,
and allowed to be wrong or out of date. Anything that becomes true and durable
should graduate into `README.md` or `docs/ARCHITECTURE.md`.

## The numbered files are closed

`02-` through `50-` are a journal of the phases that built this, and the series
ends there. **Do not add another one.** New work goes through
`openspec/changes/<name>` -- a proposal saying why, spec deltas saying what the
system will then be required to do, and a task list -- and the specs it changes
live in `openspec/specs/`. That is a better record than a journal entry because
it is structured, it is validated (`openspec validate --specs --strict`), and it
says what is *true now* rather than what happened on a Tuesday.

The numbered files stay because roughly fifty comments in the C++ cite them by
name, and because the order things were done in is the useful part of them.
Read them, link to them, correct one if it is wrong -- just do not extend the
series.

Where each thing lives now:

| | |
| --- | --- |
| What the platform is required to do | `openspec/specs/` |
| Work being proposed or under way | `openspec/changes/` |
| Known gaps, unscheduled | `backlog.md` and `backlog/` |
| Choices made, so they are not re-litigated | `decisions.md` |
| How the phases got here | the numbered files below -- closed |

The index below is partial; it stopped being maintained around phase 38. The
directory listing is the complete one.

- `02-reacthost-surface.md` — next milestone: a live surface driven by Hermes
- `03-metro-bundle.md` — React actually reconciling, with Fast Refresh
- `04-text-pango.md` — the big one
- `33-dev-bundle-errors.md` — what Metro said, instead of what it compiled to
- `34-expo-dependencies.md` — what a real app's libraries do, and the one that blocked the build
- `35-expo-modules.md` — the registry filled: clipboard, linking, the manifest
- `36-expo-image.md` — an Expo view is a Fabric component with a config in front
- `37-gesture-handler.md` — a library with no portable C++, so the contract was the port
- `38-reanimated.md` — worklets, Reanimated, and a mount React Native never reported
- `39-windows-view-layer.md` — a third desktop, and the first assertions about pixels
- `40-windows-parity.md` — the Windows suite catches the other two
- `41-msvc-core.md` — React Native's C++ core on MSVC, and the Hermes nobody has built
- `42-windows-mounting.md` — the mounting manager, and the third data point for the shared walk
- `43-windows-host.md` — JavaScript renders on Windows, and the beat that cannot be installed
- `44-windows-input.md` — a click runs an `onPress`, and the two things Win32 does not hand you
- `45-windows-scrollview.md` — the wheel, and the two ways to get one wrong on Windows
- `46-windows-textinput.md` — the last component, and the peer that is a window
- `47-parity.md` — Windows stops being the platform with an asterisk
- `48-fast-refresh.md` — Fast Refresh, and the graph nobody built
- `49-windows-view-props.md` — Windows: per-corner radii and per-edge borders
- `50-multiline.md` — `multiline`, which is only the peer — and the last of these
- `backlog.md` — everything not yet scheduled
- `decisions.md` — choices made and why, so they are not re-litigated
