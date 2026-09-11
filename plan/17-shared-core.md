# Phase 17 — splitting the core from the toolkit

> **Started, 2026-09-10.** `rn_desktop_core` builds without GTK on the include
> path.

The goal is the latest Expo on macOS, Windows and Linux. The existing desktop
platforms cannot get there: React Native for macOS is at 0.81.9 and for Windows
at 0.84.0, and Expo SDK 57 needs 0.86.3, so reaching it inside a fork means a
whole-fork rebase that is not ours to drive. Being *current* is the thing this
architecture can offer that a fork finds expensive.

Which makes the question: how much of what is here is about Linux, and how much
was merely written here first?

## Asked of the compiler, not of me

`rn_desktop_core` is a target that deliberately does not link GTK. That is the
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

## What this does not yet do

The split is a directory and a build target, not a package. `react-native-linux`
still contains both halves, and a second platform would need the shared half
published separately. That is mechanical and is the next step, deliberately left
until the line had been drawn by something more reliable than opinion.

The namespace is still `rnlinux` throughout, including in core, which will read
badly from a macOS build. Renaming it touches every file and proves nothing, so
it waits for the package split.

And none of this is evidence that a second view layer is cheap. It is evidence
about the half that is *not* the view layer. The AppKit or Windows mounting
manager is roughly the work the GTK one was, which was most of this project.
