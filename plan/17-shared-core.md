# Phase 17 — splitting the core from the toolkit

> **Started, 2026-09-10.** The core builds and links with no toolkit, and owes
> a new platform exactly one seam.

The goal is the latest Expo on macOS, Windows and Linux. The existing desktop
platforms cannot get there: React Native for macOS is at 0.81.9 and for Windows
at 0.84.0, and Expo SDK 57 needs 0.86.3, so reaching it inside a fork means a
whole-fork rebase that is not ours to drive. Being *current* is the thing this
architecture can offer that a fork finds expensive.

Which makes the question: how much of what is here is about Linux, and how much
was merely written here first?

## Asked of the compiler, not of me

`basalt_core` is a target that deliberately does not link GTK. That is the
entire design: a toolkit dependency creeping into the shared half fails the
build, rather than quietly making it unshareable and being discovered by
whoever tries to use it on another platform.

Moving the candidates in and building found one real dependency immediately.
`ExpoRuntime.cpp` included `LinuxFonts.h`, because `expo-font` arrives there and
loading a font is fontconfig's business. That is not an accident to be tidied
away: fonts are one of the places a desktop platform genuinely differs, and the
fix is a seam rather than a move. `core/FontRegistry.h` declares it and
`src/FontRegistryFontconfig.cpp` implements it, in the same link-time style
React Native uses for its own http and websocket seams.

Nothing else objected, which is the useful result.

## Where the line fell

Shared, and free of any toolkit:

- The Expo runtime, and `ExpoFontLoader` with it.
- `PlatformConstants`, `SourceCode` and `StatusBar` -- the core modules written
  so far, none of which is about Linux.
- The http client. libcurl is not a desktop-specific choice.
- The component registry.

Toolkit-bound, and rightly so: the widget layer, the mounting manager, input,
scrolling, the text field, image decoding, the frame clock, Pango, the font
registry's implementation, and the host's window.

Roughly, the view layer is platform work and everything behind it is not. That
matches the shape of React Native's own C++ platform, which is the reason this
approach can be current at all.

## What a second platform actually owes

Compiling without a toolkit says nothing about linking without one: a header
that is never included cannot betray you, and an undefined symbol at link time
can. So `core/portability_probe.cpp` is a program that links the core and
nothing else, and it is built on every build, which makes the claim an assertion
rather than a description.

Linked with no platform code at all it fails on two symbols, both the font
registry. Stub those and it links and runs:

```
core links.
  modules           PlatformConstants, SourceCode, StatusBarManager
  scriptURLFor      file:///.../bundle.js
  font seam         isFontRegistered=0 generation=0
```

So the debt is one seam.

> **Correction, same day.** It is three. Linking an actual second platform in
> phase 19 found two more that this probe cannot see: `TextLayoutManager`, whose
> stub this build drops so Pango's can be the only definition, and the component
> registry, which had been a shared inline function and turned out to be the
> thing that pulls the first one in. Both are real seams and both are now
> per-platform. The probe missed them because it links the core alone and nothing
> in the core references either -- a seam is only discoverable this way once
> something uses it. See `plan/19-macos-mounting.md`. The paragraph below, about
> the core being small and the probe only staying honest if new modules are added
> to it, was right about the risk and wrong about where it would come from.
 The Expo runtime, the three core modules, the http
client and the component registry need no platform code whatsoever. Everything
else a desktop platform must bring is the view layer, which was never in
question.

That is a better answer than expected, and it is worth being precise about why
it is not the whole story: the core is currently small. It grows every time an
Expo module is ported, and each port is a chance to introduce a second seam or,
worse, a platform assumption that compiles everywhere and is wrong on two of
them. The probe is what keeps that honest, and it only stays honest if new
modules are added to it.

## What this does not yet do

The split is a directory and a build target, not a package. `react-native-basalt`
still contains both halves, and a second platform would need the shared half
published separately. That is mechanical and is the next step, deliberately left
until the line had been drawn by something more reliable than opinion.

The namespace is still `basalt` throughout, including in core, which will read
badly from a macOS build. Renaming it touches every file and proves nothing, so
it waits for the package split.

And none of this is evidence that a second view layer is cheap. It is evidence
about the half that is *not* the view layer.

> **Correction, same day.** "The AppKit or Windows mounting manager is roughly
> the work the GTK one was" is wrong, and phase 19 is the measurement. Most of
> `GtkMountingManager` was the mutation walk, which is portable and now lives in
> `core/MountingWalk.h`; the GTK suite still passes 60/60 with the walk moved out
> and not a test changed. What a platform writes is seven operations and a props
> translation. The view *layer* -- text shaping, input, scrolling, images -- is
> still most of the work, and that part stands.
