# Phase 30 — `Appearance`, and the fourth seam

> **Done, 2026-09-11.** Dark mode works on both desktops. A stock Expo app's
> list of missing native modules is down from eight to seven.

Phase 29 ran a stock Expo app on macOS and came back with a list of what it
asked for and did not get. `Appearance` was the one that mattered: it is the
first thing a real app notices, and it was missing on *both* desktops, so
fixing it once lands on both.

## Almost all of it is portable

The module, the listener list, the event, and the rule that a JavaScript
override beats the system -- none of that is about a toolkit. What *is* about a
toolkit is two functions:

```cpp
ColorScheme systemColorScheme();
void startObservingColorScheme();
```

That makes this the fourth seam, after fonts, the text layout manager and the
component registry. It is also the first one that was designed as a seam rather
than discovered as one, which is what having two platforms already buys.

The interesting half is not reading the setting. It is that
`Appearance.setColorScheme` lets JavaScript override the system process-wide,
and the override has to win every later read *and* notify listeners itself,
since nothing outside the process moved. Putting that in core means neither
platform can get it subtly different -- and `core/ColorScheme.cpp` is where the
override, the listener registry and the notification live.

## The two halves

**macOS** watches `NSApp.effectiveAppearance` with key-value observing.
Deliberately not the `AppleInterfaceThemeChangedNotification` distributed
notification, which fires for the *system* setting only -- an app told to be
dark on a light system would never hear anything. And `effectiveAppearance`
accounts for per-application overrides and the accessibility contrast variants,
which reading `AppleInterfaceStyle` out of NSUserDefaults would not.

**Linux** reads `GtkSettings:gtk-application-prefer-dark-theme` and subscribes
to its `notify::`. The more modern answer is the XDG desktop portal's
`org.freedesktop.appearance color-scheme`, which works across toolkits and
inside a Flatpak sandbox -- but it needs a D-Bus round trip and a subscription,
and GTK already reflects the portal's value into this setting on a desktop that
has one. Same answer, none of the plumbing. Worth revisiting if a sandboxed
build ever disagrees with the desktop around it.

## The first test that belongs to neither platform

`core/tests/test_appearance.cpp` is compiled into *both* suites. What it checks
is core's behaviour -- override beats system, release hands back, listeners hear
both, an empty override clears rather than meaning light -- and asserting that
in one platform's suite would leave the other unchecked while asserting it in
both would be the same file twice.

It never asks what the system is actually set to. That is the seam, it differs
by machine, and a test that depended on it would pass on a dark Mac and fail on
a light one. Instead it asks for *the opposite of whatever this machine is*,
which is a real change anywhere.

## Two things the app found that the tests did not

**`setColorScheme(null)` throws.** `ColorSchemeOverride` is
`'light' | 'dark' | 'auto' | 'unspecified'`, and null fails in the bridging
layer before reaching any platform. That was the test app's bug rather than the
module's -- but it turned up a real one next to it: the module handled
`'unspecified'` and not `'auto'`, and which spelling arrives depends on what the
app passed. Both now clear the override.

**An app that only reads the scheme is not comparable across hosts.** This
machine's Mac is dark and its GTK is light, so the same app renders differently
on each -- correctly, and uselessly for a diff. `js/appearance.js` therefore
drives the scheme through both states and ends on a known one, which makes the
transition observable whichever way a machine is set *and* leaves both hosts in
the same place. It is the ninth app in `scripts/compare_all.sh`, and it compares
with frames included.

## Where the Expo app stands now

```
BlobModule  DeviceEventManager  EXDevLauncher  ExpoGo
ExpoUpdates  NativeUnimoduleProxy  SoundManager
```

Seven, down from eight. `BlobModule` is the only one left that a real app would
miss -- it backs `Blob`, `File` and `FileReader`, so anything fetching binary
data needs it. The other six are a hardware back button, UI click sounds, Expo's
superseded legacy bridge, and three modules about running inside Expo's own
clients, which a desktop host is not.

## What is missing

**No live system switching is proven.** Both platforms subscribe, and neither
subscription has been watched firing -- that needs a human toggling the setting
mid-run, which nothing here automates. What is proven is the override path,
which uses the same notification.

**No `AppearanceProvider` or per-view overrides.** React Native lets a subtree
have its own scheme; this is process-wide only.

**Nothing reacts to it natively.** A `PlatformColor` or a system control that
should repaint on the switch does neither, because neither platform has any --
the scheme reaches JavaScript and stops there.
