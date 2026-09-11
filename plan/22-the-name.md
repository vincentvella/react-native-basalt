# Phase 22 — the name, and the package split

> **Done, 2026-09-11.** `react-native-linux` is `react-native-basalt`, split
> into three packages.

The project stopped being about Linux somewhere around phase 18. By phase 21 it
had two working platforms, a JavaScript layer that configured three, and a
package called `react-native-linux` whose main export was
`withDesktopPlatforms`. That is not a naming quibble: the name was actively
misleading about what the thing is.

## Choosing it

`react-native-desktop` is the right name and is taken -- ptmt's Qt-based fork,
seventy published versions and still around 1,850 downloads a month. Not a
squat, so not something to contest. `react-native-gtk`, `react-native-gtk4`,
`keel`, `plinth`, `quay`, `lattice`, `anvil` and every other bare English word
worth having were gone too.

**Basalt** is the bedrock layer, and it forms columns: one shared mass with
separate columns standing on it. That is the architecture, not a decoration.
`react-native-` stays on the front because it is the prefix the ecosystem
searches on, and `react-native-basalt` is a project name rather than a component
name -- which is what ruled out `react-native-desktop-core`, where there is no
single package that *is* the project.

## Three packages

Phase 17 drew the line between the shared half and the toolkit half, and
deliberately left it as a directory and a CMake target: "the split is a
directory and a build target, not a package... deliberately left until the line
had been drawn by something more reliable than opinion." Two platforms drew it.

    react-native-basalt          JS platform layer, bundler, React Native's C++
                                 core, Hermes, Expo runtime, core TurboModules,
                                 the mutation walk
    react-native-basalt-gtk      GTK4 view layer, mounting manager, host,
                                 run-linux
    react-native-basalt-appkit   AppKit view layer, mounting manager, host

Each platform package adds the shared one as a CMake subdirectory and returns
early where it cannot build, so the repository's own `cmake -B build` produces
whatever this machine can make and the two platform packages never have to know
about each other.

`BASALT_CORE_DIR` is how a platform package finds the shared one. Its default
covers both layouts that matter -- packages side by side in a checkout, and
packages side by side in a hoisted `node_modules` -- and `run-linux --build`
passes it explicitly anyway, because pnpm's store is neither.

## What the split broke, which is the interesting part

Three things worked only because everything had been in one CMake directory, and
each failed in a way that named the symptom rather than the cause.

**Imported targets are directory-scoped.** `hermes-engine::hermesvm` is created
by the shared package; the GTK package is a sibling directory, not a child, so
it could not see it. The error is "target was not found", which reads as a typo.
`IMPORTED GLOBAL` fixes it.

**`find_package(PkgConfig)` sets a non-cache variable.** `PKG_CONFIG_VERSION` is
scoped to the directory that ran it, so a package that inherited only the cache
entry got `pkg_check_modules` failing on a malformed `if` *inside*
`FindPkgConfig.cmake`, quoting arguments that mean nothing to a reader. Each
package calls `find_package(PkgConfig)` itself.

**`add_compile_definitions` is directory-scoped too.** `BASALT_RN_MINOR` is what
the React Native version guards read, and it stopped reaching the platform
packages -- so a `switch` over `TextAlignment` failed on the enum cases that only
exist in 0.87 and later. The version now travels on the `basalt_core` target as
a PUBLIC definition, which is where it should always have been.

All three are the same shape as the seams phase 19 and 20 found: **something
that was never actually shared, only co-located, and nothing noticed until a
second consumer existed.**

## The sweep

About 111 files, and two things worth recording.

`\bRN_LINUX_THIRD_PARTY\b` does not match `-DRN_LINUX_THIRD_PARTY`, because
there is no word boundary between the `D` and the `R`. One cmake argument
survived the sweep and was found by grepping again without boundaries. Worth
doing twice on any rename of this size.

And `rn_view` was both a CMake target and the token GObject builds
`rn_view_get_type` and `rn_view_parent_class` out of, inside `G_DEFINE_TYPE`.
Renaming the target renamed the type system's symbols with it. The widget API
keeps its `Rn` prefix -- `RnView`, `RnAppKitView` -- which is accurate and was
never the thing that needed renaming.

## Names that changed

| Was | Is |
|---|---|
| `react-native-linux` | `react-native-basalt`, plus `-gtk` and `-appkit` |
| `native/mac/`, `Mac*`, `RnMacView` | `native/appkit/`, `AppKit*`, `RnAppKitView` |
| namespace `rnlinux` | namespace `basalt` |
| `RN_LINUX_*` and `RN_MAC_*` | `BASALT_*`, one prefix |
| `rn_linux_host`, `rn_mac_host` | `basalt_gtk`, `basalt_appkit` |
| `rn_tests`, `rn_mac_tests` | `basalt_gtk_tests`, `basalt_appkit_tests` |
| `rn_desktop_core`, `core_probe` | `basalt_core`, `basalt_core_probe` |
| `react-native-linux-bundle` | `basalt-bundle` |
| `RNLinuxDemo`, `rnLinuxRender` | `BasaltDemo`, `basaltRender` |
| `.rn-linux/` | `.basalt/` |

`run-linux` is unchanged: `run-<platform>` is React Native's CLI convention and
the platform really is called linux.

Earlier plan documents were swept too. They now use current names for files that
still exist, which makes them greppable at the cost of being a slightly less
literal record. Where a name change was itself the finding -- `TextInput.linux.js`
in phase 09, the seams in 17 -- the document says so.
