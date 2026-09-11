# Phase 21 — the JavaScript platform layer, for three desktops

> **Done, 2026-09-11.** A real React Native app renders on macOS. Both hosts
> produce a byte-identical tree from the same source, each reporting its own
> `Platform.OS`.

Phase 20 ended with a host that could only run `js/demo.js`, a script that talks
to `nativeFabricUIManager` by hand. Not because of the view layer — that was the
smaller problem — but because **React Native's JavaScript had no name for this
platform**. `Platform.OS` comes from `Platform.ios.js` or `Platform.android.js`;
this project supplied `Platform.linux.js`, and bundling with `--platform macos`
resolved nothing and failed at the first import.

This is the step that makes the platform layer plural.

## What was actually Linux-specific

Almost nothing, which is the useful finding.

`metro-config.js` handles three kinds of React Native module that cannot work on
a platform it has never heard of: self-importing deep-import shims, modules with
no neutral fallback, and modules this project implements differently. The answer
to the first two is identical on every desktop — the shims resolve to their
`.android.js` siblings, and `ReactDevToolsSettingsManager` gets the same no-op —
and of the third kind, only `Platform` differs at all, by one string.

So the files were renamed to say so. `TextInput.linux.js` became `TextInput.js`;
nothing in it was about Linux, only about not being iOS or Android.
`ReactDevToolsSettingsManager.linux.js` likewise. `Platform.linux.js` became
`createPlatform.js` plus three two-line modules:

```js
import createPlatform from './createPlatform';
export default createPlatform('macos');
```

Three copies of the real file would have been three chances to drift, silently,
in the one place where a difference between desktops is least excusable.

The names `macos` and `windows` deliberately match react-native-macos and
react-native-windows. A library that ships `Button.macos.js` for those forks
resolves correctly here without knowing this project exists, and one that does
not is no worse off. A different name would have bought nothing and cost that.

`withDesktopPlatforms(config)` is the general entry point and enables all three
by default, since bundling is per-platform anyway and there is nothing to be
gained by making an app declare a subset. `withLinuxPlatform` still exists and
still enables only `linux`, so an app with it in its `metro.config.js` keeps
exactly the behaviour it had.

## The dev-server problem, and an ugly answer

`ReactCxxPlatform`'s `DevServerHelper` builds its bundle URL from

```cpp
constexpr std::string_view DEFAULT_PLATFORM = "android";
```

with no hook and no setting. Every desktop host therefore asks Metro for an
android bundle, and an app would be `Platform.OS === 'android'` under Fast
Refresh and its real value in a release build — a far worse trap than either on
its own. The existing answer was to rewrite `platform=android` to `linux` on
arrival, which stops working the moment there is more than one desktop.

The only thing in that URL a host controls is `app=`, which comes from
`ReactInstanceConfig::appId` and which Metro itself ignores. So the hosts now set
it to `basalt-<platform>` and the Metro plugin reads it back.

It works, it is verifiable, and it should not have to exist. The fix is a
`platform` field on `ReactInstanceConfig`, upstream — the same shape as the
`ReactCxxPlatform` work in phase 13, and worth a second attempt now that there
are two platforms that need it rather than one.

## What it renders

`js/views.js` is a React Native app made only of `<View>`: `AppRegistry`, React,
`StyleSheet`, nested flex, `space-between`, border radii, an overflowing child.
It imports `react-native` properly, so every override has to work for it to run
at all. It avoids `<Text>`, `<Image>`, `<ScrollView>` and `<TextInput>`, none of
which macOS can mount yet.

`scripts/compare_hosts.sh` bundles nothing and assumes nothing: it runs a bundle
per host, diffs the trees, and checks each host's log for the `Platform.OS` the
app reported. Both halves matter and fail differently — a bundle built for the
wrong platform can render perfectly and be wrong about everything `Platform.OS`
guards.

```
  linux: Platform.OS is linux
  macos: Platform.OS is macos

the two hosts produced the same tree:
  view tag=1 frame=(0,0 900x700)
    view tag=18 frame=(0,0 900x700) bg=#1f2129ff
    view tag=4 frame=(24,24 568x424) bg=#4d8cf2ff
    view tag=2 frame=(40,160 120x48) bg=#e6eeffff opacity=0.85
    view tag=6 frame=(608,24 268x424) bg=#f27359ff
    view tag=16 frame=(24,464 852x212) bg=#59cc8cff
    view tag=10 frame=(40,550 40x40) bg=#1f2129ff
    view tag=12 frame=(430,550 40x40) bg=#1f2129ff
    view tag=14 frame=(820,550 40x40) bg=#1f2129ff
```

Bundling for `windows` works too. There is no Windows host to run it, so what
that proves is only that the JavaScript layer is not accidentally two-platform —
which, given how this file started, was worth checking.

## The flat tree is not a bug

That tree looks wrong. `badge` is a child of `left` in the JSX, and it appears
as its sibling with absolute coordinates. It is worth writing down, because it
looks exactly like a mounting manager parenting everything to the root.

It is React Native's view flattening. `ViewShadowNode` computes two traits:
`FormsView`, which a `backgroundColor` is enough for, and
`FormsStackingContext`, which it is not — that needs opacity, a transform,
clipping, an event handler, `collapsable={false}`, and so on. Children are
hoisted out of any node that does not form a stacking context, with their
coordinates rebased onto the nearest one that does. `left` paints, so it forms a
view; it has nothing that makes it a stacking context, so `badge` is hoisted past
it.

Both platforms produce it identically, which is how it was confirmed rather than
assumed.

## What is still missing

**`run-macos` does not exist.** `react-native.config.js` now declares all three
platforms, so `react-native bundle --platform macos` is accepted, but only
`run-linux` is registered as a command. A run command needs the host to be
buildable from inside an app rather than from this repo, which is the shape of
the next CLI step.

**Dev mode is untested on macOS.** `BASALT_DEV` is wired through exactly as the
GTK host does it, and the `app=` correction above is what should make it work,
but nothing has run it: Fast Refresh needs components macOS cannot mount yet
before the result would mean anything.

**The package is still called `react-native-basalt`**, and the function that
configures three platforms lives in it. That is now the largest piece of naming
debt in the repository, and renaming an npm package, a repo and every document
is a decision rather than a refactor.

**One flaky end-to-end scenario.** In five consecutive runs of
`scripts/integration_test.py`, the TextInput focus scenario failed once with "the
focus command did not move focus to the field" and passed the other four times.
It is a timing assumption in the test — taps are scheduled at fixed delays —
rather than anything this phase changed, and it is in `plan/backlog.md` now
rather than being quietly tolerated.
