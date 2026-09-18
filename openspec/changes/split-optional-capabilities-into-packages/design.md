# Design

## Context

See proposal.md - Why.

The constraint that makes this different from React Native's situation: there is
no dynamic native module loading here. A host is a single binary, built by
`react-native run-<platform> --build`, and anything native has to be compiled
into it. So "a separate package" cannot mean "a package the host knows nothing
about" -- it has to mean a package the *build* discovers.

That mechanism already exists and is proven three times over. `desktop.js`
finds `expo-modules-core`, `react-native-worklets` and `react-native-reanimated`
in the app's `node_modules` and passes each as `-DBASALT_<NAME>=<dir>`; the
host's CMake compiles their C++ when the define is present. What it is not is
general: the CLI knows those three by name.

## Goals / Non-Goals

- Goal: a rule for the boundary that can be applied without re-litigating it per
  capability.
- Goal: any package can contribute native code to the host build, discovered
  rather than hardcoded.
- Non-Goal: splitting the three host packages. `react-native-basalt-gtk`,
  `-appkit` and `-win32` are the hosts; that boundary is already right.
- Non-Goal: matching Electron's module list. Electron is a reference for what a
  desktop does, not for how a package is shaped.

## Decisions

**The rule: core is what an app cannot avoid needing, and what nothing else can
provide.**

Three tests, and a capability has to fail all three to stay in core:

1. *Could an app ship without it?* A window cannot be avoided -- the app runs in
   one. A tray icon can.
2. *Does React Native already have the API?* `Clipboard`, `Share`, `Linking`,
   `Appearance` are React Native's own APIs that this platform implements. Those
   belong in core because the app imports them from `react-native`, not from us.
3. *Does it need a seam nothing outside core could reach?* Mounting, input
   routing and the surface lifecycle are core by construction.

By that rule what is shipped today sorts as:

- **Core**: the component surface, input, the window (lifecycle and geometry),
  the React Native APIs this platform implements, developer tools, packaging.
- **Its own package**: menus, context menus, file dialogs, the title bar.

The last line is the uncomfortable one, and worth stating plainly rather than
softening: `<Menu>`, `useDialog()` and `<TitleBar>` are all things an app can
ship without, and by the rule they do not belong in core. Moving them is a
breaking change. It costs almost nothing today and is exactly what React Native
could not afford later.

**A contributing package declares itself in its own manifest**, rather than the
CLI knowing its name. A `basalt` key naming a CMake entry point is enough:

    "basalt": { "native": "native/CMakeLists.txt" }

The CLI scans the app's dependencies for that key and passes each directory in,
which is what `optionalNativeModules` does now with a hardcoded list. Expo,
worklets and Reanimated keep their special cases only because they are
third-party packages that will never carry a `basalt` key.

**Version together, at first.** Separate packages do not have to mean
independent versions, and independent versions of packages that share a C++ ABI
with the host is a support problem nobody here wants yet.

## Risks / Trade-offs

- More packages is more install friction, which is the cost `npx
  react-native-basalt init` exists to absorb. If `init` does not add the
  packages an app needs, the split is worse than not splitting.
- A wrong boundary is harder to fix than no boundary, because moving twice is
  worse than moving once. This is why the rule is written down rather than
  applied case by case.
- Discovery by manifest key means a package can contribute C++ to a host binary
  by being installed. That is the same trust model as any native module on iOS
  or Android, and worth saying out loud.

## Open Questions

- Do menus and dialogs move, or does the rule get a fourth test that keeps them?
  The rule as written moves them; that may be the wrong answer and it should be
  argued rather than assumed.
- Does a capability that only one desktop can do -- the title bar is Windows
  only today -- belong in a package per desktop, or one package that reports the
  others as unsupported? The second matches how `capabilities` already works.
