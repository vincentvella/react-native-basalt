# Phase 47 — parity

> **Done, 2026-09-12.** Windows stops being the platform with an asterisk. It
> runs the same end-to-end suite, it has a `run-windows`, `compare_hosts.sh`
> knows how to run it, and the two remaining Windows-only rendering gaps are
> closed.

Phases 39 to 46 built the Windows port a component at a time, and each one ended
with a list of what the other two desktops had and this one did not. This phase
is that list.

```
running 5 scenarios against main.jsbundle.js on windows
input: injected -- taps injected at the dispatcher, skipping the window system
  ok    initial render
  ok    scrollToEnd, and a tap that bubbles from a label
  ok    scroll away and back
  ok    focus a TextInput, type, and see it round-trip through React
  skip  edit the demo and watch Fast Refresh apply it
        scripts/metro.sh has no Windows path

4/4 passed, 1 skipped
```

## The end-to-end suite, and the two bugs it found on its first run

`scripts/integration_test.py` grew a `--platform` and picks whichever host is
built. That is all it needed: its scenarios are about React and Fabric rather
than about a toolkit, so the tree a tap produces is the same tree on all three.
That is the claim `compare_hosts.sh` makes; this suite is the first thing to
rely on it.

It earned its keep immediately, which is the part worth writing up.

**The host crashed on exit whenever an app rendered an `<Image>`.** Every time,
`0xC0000005`, and nothing had noticed because every run so far had been watched
rather than checked. WIC decodes on a worker thread that calls `CoInitializeEx`,
and the `IWICBitmap` it produces is released on the UI thread when the view
holding it is deleted — a thread that had never initialised COM at all. `main`
now opens an apartment before anything else and closes it after `shutdown`.
`ShellExecute` wanted one too, which is how `core/PlatformServices` opens a URL,
so the same two lines fix a second thing nobody had tried.

**The Windows tree reported nothing for a `<TextInput>`.** GTK prints
`editable="..."` and `focused`; Windows printed neither, because a field's
content lives in its EDIT peer and the view had no way to reach it. So
`compare_hosts.sh` would have called a working text field a difference the first
time anyone ran it with two hosts. `RnWin32View` now keeps the same back pointer
to its peer that GTK keeps to its `GtkText`.

That fix needed a second one. The dump is taken on the way out, and destroying
the host window destroys every child window with it — so a tree written from
`shutdown` reports every field as empty and unfocused, which reads as a broken
text field and is a dead `HWND` being asked a question. `captureBeforeTeardown`
runs at `WM_CLOSE` instead, where nothing has been destroyed yet.

And *that* introduced a use-after-free, which the same suite caught within
minutes: `MountingWalk` calls `destroyView` before `forgetTag`, and
`releaseAllViews` runs before the text input manager's own destructor, so both
teardown paths free the view first. Clearing the view's back pointer from
`destroyPeer` reaches into freed memory. Nothing needs clearing anyway — the
view never outlives its peer.

Three bugs, one of them mine, none of them findable by a unit test, all of them
found by running the app the way a person would and then *checking*.

## `compare_hosts.sh` learns to count past two

It runs whichever hosts are built and diffs every pair against the first. Two
are still needed for there to be a comparison, which is still the thing this
machine cannot supply: WSL2 will not start here because virtualisation is
disabled in firmware. What changed is that the script is no longer written for
exactly two.

It also takes an app name rather than a bundle path per platform. Three paths on
a command line is one too many, and the convention —
`build/<app>.<platform>.jsbundle.js` — is what `compare_all.sh` was constructing
by hand anyway.

The Windows leg was exercised on its own: it produces a tree, reports
`Platform.OS is windows`, and prints `editable=""` where GTK does. What has
still never happened is two hosts on one machine, and only that turns "the dumps
are written to match" into "the dumps match".

## `run-windows`, and `run-macos`, and one command with three names

Everything a desktop run does is the same on all three: find or build a host,
work out the module name, make sure a packager is running, write a bundle,
launch in the foreground, hand back the exit status. What differs is four
strings — the platform, the command name, the binary, and where its native
sources are.

So the GTK CLI's five hundred lines moved into
`react-native-basalt/cli/desktop.js`, and each platform package is now a
twenty-line file that names its four strings. The same split as the C++ half,
for the same reason: three copies would be three places for `--mode release` to
mean something slightly different, and the first anyone would hear of it is an
app that bundles without assets on one desktop.

One file per package cannot be shared, and it is the one that finds the shared
package. `cli/shared.js` is that bootstrap.

Two things are genuinely per-platform and are passed in rather than branched on.
The binary carries an extension on Windows, so `isExecutable` has to stop asking
about the executable bit — a filesystem that has none answers yes for every
readable file, and asking anyway would call a text file runnable. And
`bootstrap.sh` is invoked through `bash` by name there, because a `.sh` file is
not executable on Windows and Git Bash is what it detects and expects.

`scripts/test_cli.js` is the first JavaScript test in the repository — eight
cases over the pure decisions: where the host is looked for and in what order,
that a missing one explains how to build one rather than printing a path, where
the module name comes from, and that all three commands offer the same options.
It runs in both CI jobs, because it is the same function on both and the half
only Windows can be wrong about is exactly the `isExecutable` one.

## Two rendering gaps, closed

**Per-run text colour.** DirectWrite carries colour as a *drawing effect* rather
than as a range attribute like family or weight, so `Hello <Text
style={{color:'red'}}>world</Text>` rendered entirely in the first colour. That
looks like it needs a custom `IDWriteTextRenderer` — and it would, for anything
else you might attach. Direct2D's own renderer has exactly one special case: a
drawing effect that is an `ID2D1Brush` is used to paint that range. So the fix
is a loop and a vector of brushes, and the test is a picture, which is the one
thing this platform can assert and the other two cannot.

**The uncontrolled-`<TextInput>` bug**, found on Windows in phase 46, is fixed
on GTK and macOS with the two tests that pin it on each. Both applied the `text`
prop whenever it differed from the widget, which is wrong for a field JavaScript
does not own: React Native re-sends `mostRecentEventCount` on every change, that
alone is an Update mutation, and an uncontrolled field's `text` is the empty
string forever. Neither had a staleness guard on the prop either, only on the
command.

## What is left, and what it is not

Not a missing half. Windows now mounts every component, runs every scenario the
other two run, and has the same command. What remains is named in
`plan/backlog.md` and most of it is shared with at least one other desktop:
multiline `<TextInput>`, `onKeyPress`, a tab order, momentum scrolling,
`offsetPoint` in view coordinates.

Two things are Windows' alone and worth repeating here. A `<TextInput>`'s peer
is a child window, so it is always on top and cannot be rotated or clipped by
the React tree — it is hidden when it leaves a scrolled ancestor instead.
And the choreographer is a sixteen-millisecond timer rather than a display link,
which needs `DwmFlush` on a thread of its own.

One gap is about this repository rather than about Windows. **CI does not build
the Windows host.** Its job is the view layer only — two minutes, no React
Native, no Hermes — so 63 of the 149 Windows tests run there, and the mounting
manager, the input path, `<ScrollView>` and `<TextInput>` are covered by a
developer's machine alone. A full job means vcpkg, a React Native checkout and a
Hermes build on a Windows runner: an hour cold, and worth doing once its caching
is understood well enough not to pay that every run.
