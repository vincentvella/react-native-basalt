# `ReactCxxPlatform` is not in the npm package

> **Solved locally, 2026-09-10. Nothing submitted upstream.**

An app can now build the host from an installed React Native. This document is
why that took work, and what the upstream fix would be if it is ever worth
asking for.

`react-native run-linux --build` needs a React Native *source checkout*. Every
app has an installed React Native and almost none has a checkout, so that is the
gap between a command that works here and a command that works for anyone.

The cause is one line, and this document is the evidence for changing it.

## The claim

`packages/react-native/package.json` lists `ReactCommon`, `ReactAndroid` and
`ReactApple` under `files`. It does not list `ReactCxxPlatform`. So an installed
React Native contains everything needed to build a C++ host except the C++ host
layer: `ReactHost`, the scheduler delegate, `ReactCxxTurboModuleProvider`, the
http, websocket and dev-support seams, and the core modules.

## The measurements

From `npm pack --dry-run` on 0.87.1, which is the real packing process rather
than a reading of the manifest:

| | files | size |
|---|---|---|
| whole package | 4244 | 18.9 MB unpacked |
| `ReactCommon` | 1418 | 6.0 MB |
| `ReactAndroid` | 1232 | 4.6 MB |
| `ReactApple` | 14 | tiny |
| **`ReactCxxPlatform`** | **0** | **absent** |

Adding the one line takes the package to 4397 files and 19.5 MB. That is 98
files and 206 KB, about 1% of the unpacked package.

## That it is the only thing missing

Checked rather than assumed. Every path this platform's build references under
React Native, 103 of them, against the packed file list:

- 15 missing, all under `ReactCxxPlatform`.
- 2 missing elsewhere, both ResizeObserver directories that do not exist in
  0.87.1 at all because they landed on `main` afterwards, and which this build
  already skips.

Everything else it needs already ships: the `ReactCommon` subtrees,
`scripts/generate-codegen-artifacts.js`, `sdks/.hermesversion` and
`gradle/libs.versions.toml`.

## That it actually works

The path check is not proof, so it was built. `npm pack` on 0.87.1, `npm install`
of the resulting tarball into an empty project, `ReactCxxPlatform` copied in as
the only modification, and then this platform's bootstrap and CMake pointed at
`node_modules/react-native`.

Result: a 37MB `rn_linux_host`, which renders a React application with no
JavaScript errors. Built entirely from an installed React Native.

## The second gap, which the build found and the path check did not

`ReactCommon/react/nativemodule/cputime` ships its C++ but its codegen spec does
not ship: the spec lives at `src/private/testing/fantom/specs/NativeCPUTime.js`,
under a testing directory that is reasonably excluded. So the package contains
C++ that cannot be compiled from the package, and the failure is a missing
`NativeCPUTimeCxxSpec` template, which reads like a codegen bug rather than a
packaging one.

Nothing links that target -- it is a Fantom testing module -- so this platform
now simply does not build it. Worth mentioning upstream as a smaller,
independent inconsistency, and worth keeping out of the main ask so the main ask
stays a single line.

This is also the reason to build rather than to reason. The path check said one
directory was missing and the path check was right about that and incomplete.

## The ask

Add `"ReactCxxPlatform"` to `files` in `packages/react-native/package.json`:

```diff
     "!ReactAndroid/src/test",
     "ReactApple",
     "ReactCommon",
+    "ReactCxxPlatform",
     "README.md",
```

It costs about 1% of the package, and it unblocks any out-of-tree C++ platform
from being built against an installed React Native rather than a checkout.
Nothing in the 22 existing issues and pull requests mentioning `ReactCxxPlatform`
raises the packaging question.

## What we do instead

Nothing was opened upstream. A one-line packaging change with one obscure
consumer is easy to ignore, and this platform has no users yet to point at, so
the ask would sit. It is written down here for when that changes.

The workaround is to fetch the directory rather than to vendor it. When React
Native is an installed package and has no `ReactCxxPlatform`, bootstrap does a
sparse, blobless clone of that one directory at the tag matching the app's exact
version: about 3MB and four seconds, which is a smaller bargain than the folly
and Hermes downloads it already makes.

**Fetching rather than vendoring is forced, not preferred.** Carrying a copy in
this package was the obvious answer and it cannot work. That layer tracks
`ReactCommon` closely: `main`'s copy fails against 0.87.1 on a ResizeObserver
header that did not exist and a `ReactInstance::createJSCallInvoker` that had not
been added. So one copy cannot serve several React Natives, and a vendored copy
would pin this package to a single version the way react-native-windows pins to
one exact nightly. Fetching at the app's own version keeps the C++ and the
JavaScript in step by construction, which is the property the previous phase
existed to establish.

Verified from a stock `npm install react-native@0.87.1` with nothing copied by
hand: bootstrap fetches, CMake builds, and the app renders.

The cost is a network fetch on first build and a dependency on the tag existing.
A React Native installed from a nightly or a fork has no matching tag, and
bootstrap says so rather than guessing at a near-enough version.
