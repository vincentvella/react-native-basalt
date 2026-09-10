# Getting `ReactCxxPlatform` into the npm package

> **Evidence gathered, 2026-09-10. Nothing submitted yet.**

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

## Not submitted

Nothing has been opened upstream. Submitting is a decision for the repository's
owner, and it needs their identity and their agreement to Meta's contributor
licence terms.
