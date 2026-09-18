# Testing

Part of the [backlog](../backlog.md). Not scheduled.

- **No rendering assertions on GTK or macOS.** The widget tree says a view has a
  colour and a frame, not that the right pixels reached the screen. This is not
  theoretical -- GTK's cairo renderer mangled every transform in the demo and no
  test noticed. See `docs/TESTING.md`.

  Windows has them as of phase 39, because Direct2D renders offscreen with no
  window and no display and the other two toolkits do not. So the question is no
  longer what to assert -- `tests/test_win32_paint.cpp` is the list -- but what
  each of the others costs: a display server and a `GdkTexture` read-back on
  GTK, an offscreen `NSWindow` and a display cycle on macOS. The macOS snapshot
  path in `AppKitSnapshot.mm` already does the hard half of the second one.
- Nothing exercises the JS thread and the main thread concurrently.
- The end-to-end scenarios hard-code tap coordinates from the demo's layout.
  Finding a button by its label in the dumped tree would survive a restyle.
- CI builds and tests Linux and Windows on every push. macOS is built only by
  `release.yml`, which has not run yet, and otherwise by whoever is developing
  on a Mac.
- **The Fast Refresh scenario is skipped in CI**, so nothing on a machine
  protects development mode. Metro on a GitHub runner never notices an edit:
  the file changes on disk with a fresh mtime, a newly requested bundle still
  carries the old text, and Metro logs nothing. Ruled out already: `fs.watch`
  sees the same edit on the same runner; the inotify limits are 655360 watches
  and 1280 instances; both sides run Node 24.20.0; and CI's layout, with React
  Native inside the checkout, reproduces green in a local VM. The scenario
  passes on macOS and on Linux in a VM, so this is about the runner rather than
  the code.

  **Watchman was the standing theory, and it is now ruled out.** It was tried
  on the `ci` branch and did not fix the scenario. What the run established,
  so that none of it needs doing again:

  - The upstream build was used, not Ubuntu's 4.9.0 from 2017, so a failure
    here says something about the theory rather than about an ancient client.
    It has to come from a GitHub release: watchman is not in apt, and
    facebook/watchman stopped attaching release assets after v2026.07.27.00,
    which is the last version that ships a Linux binary at all.
  - It ran. `watchman --version` answered `20260727.012849.0`, and
    `watchman watch-project js` reported `"watcher": "inotify"` over exactly
    the root Metro was given.
  - A `.watchmanconfig` was added at the project root -- React Native ships one
    and this repo never had it -- so watch-project resolved the root Metro
    asked about rather than some parent. That file is still in the repo, since
    it is correct regardless of CI.
  - The scenario failed the same way, and `serves_edit` answered **no**: a
    freshly requested bundle still carried the old text, so Metro had not seen
    the change at all.

  What that leaves is the one thing the run did not establish: whether
  *metro-file-map* used watchman, or found it and fell back anyway. The
  `watch-project` call above was made by the workflow, not by Metro, so a watch
  existing proves nothing about which watcher Metro chose.

  Next thing to try, in order of effort:

  1. **`DEBUG=metro:*` on the CI Metro**, to see what the watcher thinks it is
     doing rather than inferring it from what the bundle contains -- starting
     with whether it picked watchman at all, which is the question the run
     above left open.
  2. Shrink `watchFolders`. The React Native checkout is the bulk of the eight
     thousand directories; if the watcher is falling over on volume, a narrower
     watch would show it.
  3. Have the scenario poll Metro for the edit rather than waiting on the host,
     which would at least separate "Metro never saw it" from "Metro saw it and
     the client missed it" without another CI round trip per hypothesis.
- **The GTK `<TextInput>` focus scenario flaked on a Mac, and nothing explains
  it.** `focus a TextInput, type, and see it round-trip through React` failed
  three runs in a row on 2026-09-13 with *"the focus command did not move focus
  to the field"*, then passed six in a row, on the same build and the same
  machine. No change was made between the two states that touches focus.

  It was running the **GTK host on macOS**, over the quartz backend, which
  `docs/HANDOFF.md` already warns is not the target: keyboard focus there is
  the window server's to give, and the scenario needs the window to have it
  before `TextInput.focus()` can mean anything. CI, on real Linux under Xvfb,
  was green across the whole period and has never reproduced it.

  What was ruled out: it is not a code change. The one edit in flight touched
  `main_gtk.cpp` and was reverted, rebuilt and re-run, and the failure
  survived that -- so it was already failing before anything that day touched
  the host. The suspicion is that a pile of stray `basalt_gtk` and
  `basalt_appkit` processes from earlier runs were holding or stealing focus,
  since killing them is the only thing that happened between the last failure
  and the first pass. That is a correlation and nothing more; it was not
  tested by reproducing it.

  Worth doing before trusting it: reproduce deliberately -- leave a host
  running and start another -- and if that is it, have the scenario fail with
  "another host is already running" rather than with a focus error, which is
  the misleading half. If it cannot be reproduced that way, the next suspect
  is the quartz backend itself, and the answer is that this scenario should
  not be believed off a real Linux session at all.

- CI has no rendering assertions, so it cannot catch what the cairo renderer did.


- **Nothing tests tap-to-focus.** Clicking a `<TextInput>` focuses it on both
  hosts -- verified with a real `CGEvent` mouse click, after which a keystroke
  round-trips -- but no automated test can check that, and two obvious ways of
  trying give a false negative.

  `BASALT_TEST_TAP` enters at the touch dispatcher, below the window system, so
  it moves React Native's responder but never reaches the peer widget that
  actually takes focus. That is the same deliberate limitation `BASALT_TEST_TYPE`
  has with the key controller, and it looks exactly like a broken feature: the
  `<Pressable>` beside the field responds to an injected tap and the field does
  not. System Events' `click at` is no better -- it performs an accessibility
  press, which is why it answers with the name of the element it found, and a
  text field does nothing with one.

  So a tap-to-focus regression would be invisible: `integration_test.py`'s focus
  scenario taps a *button* that calls `focus()`, and every `<TextInput>` feature
  added in phases 51 and 52 was probed by focusing programmatically. Testing it
  needs a real click, which on macOS means `CGEventPost` and the accessibility
  permission that goes with it, and on Linux means xdotool -- which CI already
  has, and which is where this is worth adding.

- **A `<TextInput>`'s wrapper is still an element of its own on Windows.**
  Fixed on the other two in phase 53: the accessible name lands on the peer,
  which is what a screen reader reaches, and the wrapper leaves the tree --
  `accessibilityElement = NO` on AppKit, and `GTK_ACCESSIBLE_ROLE_PRESENTATION`
  chosen at construction on GTK, which is the only moment a GtkAccessible role
  can be chosen at all.

  Windows has not been looked at. UI Automation is the one that works
  differently -- a provider answers questions rather than a view carrying
  properties -- so the question there is whether the wrapper's provider should
  refuse to be a control, and whether the `EDIT` peer is exposed as its own
  element at all. `RnWin32Accessible.cpp` is where it would go.

  Whatever the answer, it must not print in `describeTree`: the other two say
  nothing about this view and a third vocabulary would put the cross-host diff
  back where phase 53 found it.

- **No unit test can observe an event.** Neither suite attaches an
  `EventEmitter` to the shadow views it builds, and `test_appkit_textinput.mm`
  says so in as many words: *"No emitter is attached to these hand-built shadow
  views, so what this pins is that the round trip did not throw."* That is a
  reasonable thing to pin and it is not coverage of the event.

  So nothing about `onChange`, `onFocus`, `onBlur`, `onKeyPress`,
  `onSelectionChange` or `onSubmitEditing` is tested anywhere below the
  end-to-end suite. Three of the four `<TextInput>` features added in phases 51
  and 52 rest on probe apps run by hand -- which is good evidence that they
  worked once, on the machine they were written on, and no evidence at all
  against a regression.

  A stub emitter handed back through `EmitterLookup` was the obvious fix and it
  does not work, which is worth recording so it is not tried twice.
  `TextInputEventEmitter` has no virtual methods at all -- the header contains
  the word `virtual` zero times -- so there is nothing to override, and
  constructing a *real* one reaches `EventDispatcher`, which needs an
  `EventBeat`, which needs a `RuntimeScheduler` and so a JavaScript runtime.
  That is the whole machinery the unit suites exist to avoid.

  What would work is a seam in the managers: route every emission through one
  private `emit(tag, kind, metrics)` that calls the emitter by default and can
  be pointed at a recorder in a test. Thirty-odd lines across the two managers,
  and it is production code carrying a test hook -- a trade worth making
  deliberately rather than by accident, which is why it is written here rather
  than done. `onKeyPress` firing before `onChange` is the case that most wants
  it, being an ordering guarantee nothing currently checks.

- **The hover scenario cannot assert its order on GTK-over-quartz**, and is
  skipped there with a note rather than loosened. A real cursor sitting over the
  window when it maps has already entered the card before the first scripted
  move lands, so the `enter card` the sequence expects in the middle arrives at
  the start -- and again at the end. Deterministic rather than flaky: three runs
  byte-identical. CI runs this host under Xvfb, which has no pointer, and
  asserts the full order, so nothing was weakened where it counts.

- **One flaky end-to-end scenario.** "focus a TextInput, type, and see it
  round-trip through React" failed once in five consecutive runs of
  `scripts/integration_test.py` with "the focus command did not move focus to
  the field", and passed the other four. The scenario schedules taps at fixed
  delays and assumes the host has caught up, which is a timing assumption rather
  than a synchronisation. Fixing it means waiting on something observable --
  the tree, or a log line -- instead of on a clock.
