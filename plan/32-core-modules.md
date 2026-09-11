# Phase 32 — the modules that crashed an app, and the ones that lied

> **Done, 2026-09-11.** Six modules, on both desktops. Ten checks pass on each,
> and nothing a real app imports throws any more.

Phase 29's Expo trial left a list, and phases 30 and 31 took the two that
mattered most off it. What was left was older than that list: a set of core
React Native modules with no implementation anywhere in this stack, sitting in
`plan/backlog.md` since phase 07.

## Two ways to be missing, and the quiet one is worse

`Clipboard` and `Vibration` are looked up with `TurboModuleRegistry.getEnforcing`.
An app that so much as *imports* either one died at startup -- no error boundary,
no first paint, a dead window. That is the loud kind, and it is the kind that
gets fixed.

The rest are looked up with `get`, which returns null, and React Native's
JavaScript then falls back or quietly does nothing. `Alert.alert()` was a
confirmation dialog that never appeared, so the code after it never ran.
`Linking.openURL()` was a link that never opened. Neither said anything. That is
the kind that gets shipped.

Doing both at once was the point: the second set was less obviously urgent and
is the one an app would actually be wrong about.

## What each of them needed

Six modules, and only three touch an operating system at all:

| Module | Portable? |
|---|---|
| `Clipboard` | seam: `NSPasteboard` / `GdkClipboard` |
| `AlertManager` | seam: `NSAlert` sheet / `GtkAlertDialog` |
| `LinkingManager` | seam: `NSWorkspace` / `g_app_info_launch_default_for_uri` |
| `Vibration` | portable -- a desktop has no motor, so doing nothing is the truthful implementation rather than a placeholder |
| `I18nManager` | portable -- Yoga already does right-to-left; this is the switch |
| `AccessibilityInfo` / `AccessibilityManager` | portable |

So `core/PlatformServices.h` is the fifth seam, after fonts, the text layout
manager, the component registry and the colour scheme -- and it is one file with
five functions rather than three seams, because each is two or three lines and
splitting them would have meant six headers saying very little.

## The alert, and a mistake worth recording

The first macOS implementation used `[NSAlert runModal]`. That is wrong, and not
subtly: `runModal` spins its own run loop on the main thread, which stops the
main queue -- so every mount transaction, timer and animation frame would be
held until somebody clicked a button. It is now a sheet, through
`beginSheetModalForWindow:completionHandler:`, which returns at once and answers
through its completion handler. The GTK side was already asynchronous, because
`gtk_alert_dialog_choose` is.

`showAlert` still falls back to `runModal` when there is no window to attach a
sheet to, which is a real case -- an alert before the first window is on screen.

That fallback is also why **there is no unit test for the alert**. A
command-line test binary's activation policy is `prohibited`, so its windows can
never become key or main, so `showAlert` takes the fallback and puts a genuine
modal dialog on the developer's actual desktop. It did exactly that, twice,
while this was being written, and the second time because a build error had left
a stale binary in place and the test that was supposed to be gone ran anyway.

What the alert needs proving is that it does not block, and `js/alert.js` proves
it better than a unit test could: a 300ms heartbeat keeps ticking while the
alert is up, which a blocking implementation could not do.

## `AccessibilityInfo.js` takes the iOS branch

`isScreenReaderEnabled()` threw `NativeAccessibilityManagerIOS is not available`
even with `AccessibilityInfo` implemented, because that is not the module it
calls. `AccessibilityInfo.js` branches on `Platform.OS === 'android'` in eight
places, and a platform that is neither takes the else branch every time -- the
iOS path, calling `NativeAccessibilityManagerIOS`.

This is the third time: `TextInput.js` in phase 09, `ImageViewNativeComponent`'s
view config in phase 26, and now this. **React Native's JavaScript branches two
ways, and a third platform lands on iOS.** Worth expecting rather than
rediscovering -- it is now a line in the backlog as a pattern rather than three
separate incidents.

Both spellings are implemented. The iOS one is what gets used; the Android one
is kept because an app can reach it directly and because which one React Native
picks is a JavaScript decision that could change.

## Where honesty was the implementation

Several of these answer rather than act, and each answer is a decision:

**Every `AccessibilityInfo` state is false.** Neither desktop exposes "is a
screen reader running" without asking the accessibility bus, and answering
*true* wrongly is worse than false: React Native's own components would change
behaviour, announce things and alter focus order for a user who is not there.

**`I18nManager.isRTL` is only ever what `forceRTL` set.** Deciding it from the
system locale needs a locale database this does not have, and guessing wrong
flips an app's entire layout.

**`getInitialURL` resolves to null**, not to an empty string, because null is
what React Native's JavaScript checks for.

**`openSettings` rejects.** There is no per-application settings page on a
desktop to open, and resolving would be a lie.

**GTK's `clipboardText` only sees this application's own clipboard.**
`GdkClipboard` reads asynchronously -- it may have to ask another process -- and
the seam is synchronous because `Clipboard.getString()` resolves at once. Reading
another application's clipboard needs an async path core does not yet hand down.
Said plainly in the file rather than returning an empty string that looks like
an empty clipboard.

## What it gets to

`js/modules.js` runs ten checks on both hosts, and the first is the one that
matters most: that importing `Clipboard` and `Vibration` does not throw. The
rest cover the clipboard round-tripping unicode, `canOpenURL` saying yes to
https and no to nonsense, `getInitialURL` resolving rather than hanging, and
every `AccessibilityInfo` question answering.

Eleven apps now, and `scripts/compare_all.sh` says the two hosts agree on all of
them.

## Two more, found by asking instead of reasoning

The first pass through this phase declared victory on six modules. It was
wrong, and the way it was wrong is the point: the list of what to implement came
from `plan/backlog.md`, which came from a `grep` for `getEnforcing<...>('Name')`
that only matched single-line calls. A multiline-tolerant sweep found
twenty-seven names rather than fifteen.

So `js/probe.js` exists now: it touches every API an app might reach -- web APIs,
React Native modules, components -- each inside its own try/catch with `require`
at call time, because a top-level import that throws takes the app down, which is
exactly the failure being looked for. It reports rather than asserts.

It found two more real crashes and one thing that was not one.

**`ToastAndroid`** throws on import. Android's toast is a transient message over
the app; a desktop has no such thing built in, and the nearest equivalent is a
system notification, which is a different thing with a different lifetime. The
message is logged rather than shown -- not a good answer, and a better one than
throwing, because an app showing a toast is telling the user something and
losing it silently is worse than losing it visibly.

**`DevSettings`** throws in a *release bundle*, which is the worse of the two.
Upstream provides it only when a dev server exists, so `DevSettings.reload()` --
reachable from ordinary code -- is a production crash. Ours fills that gap and is
offered **only when dev mode is off**, because this provider is consulted before
upstream's and offering it unconditionally would shadow the one that actually
works.

And the one that was not a crash: `InteractionManager` throws
"InteractionManager has been removed from react-native core", on every platform.
That is upstream's intent, not a gap here. It is out of the probe, with a note
saying why, because a diagnostic that reports a deliberate removal as a failure
will get someone to "fix" it.

Twelve apps now, and the probe reports 27 of 27 on both hosts.

## What is missing

**Alert prompts on Linux.** `GtkAlertDialog` has no text field, and building one
means a `GtkDialog` with its own layout. Not done -- a prompt that silently
became a plain alert would lose whatever the user was meant to type, so it warns
instead. macOS has it, through an accessory view.

**`login-password` alerts**, on both: two fields and a layout, and answering
with one would silently lose the password.

**Incoming URLs.** Nothing registers a scheme handler or parses one off the
command line, so `Linking.addEventListener('url')` never fires and
`getInitialURL` is always null.

**Reading another application's clipboard on Linux**, above.

**`ShareModule` and `ToastAndroid`** are still absent. Share is a real gap;
ToastAndroid is Android's and only throws if an app imports it directly.

**`setAccessibilityFocus` and `announceForAccessibility` do nothing.** Both need
the platform's accessibility API to post a notification for a specific view,
which neither mounting manager exposes by tag.
