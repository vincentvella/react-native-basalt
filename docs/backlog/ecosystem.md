# Ecosystem

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. Nobody else can use this yet
2. ~~Adding a desktop to an Expo app is manual~~ -- done, `npx react-native-basalt init`
3. Porting a first third-party native module end to end
4. Packaging: Arch PKGBUILD, Flatpak
5. The package ships no types, and an Expo app is TypeScript by default

- **Nobody else can use this yet** -- because nothing is published, and no
  longer because installing would not work. A fresh `create-expo-app` (SDK 57,
  React Native 0.86.3) with the core packages installed from their tarballs
  runs `react-native run-linux --build` and renders the template's text, with
  nothing from this repository on the path. `.github/workflows/release.yml`
  does exactly that on every release. Three things stood in the way, all in
  `--build`, and all found by writing that job:
  - ~~**It demanded a React Native source checkout.**~~ `buildHost` refused an
    installed `react-native`, long after bootstrap had learned to fetch the
    one directory the npm package lacks and CMake to build from the rest (see
    the `ReactCxxPlatform` entry under Upstream). Its error message told people
    to pass `--react-native-path`, which is not a flag.
  - ~~**It never wired in Expo.**~~ Nothing passed
    `-DBASALT_EXPO_MODULES_CORE`, worklets or Reanimated, so an Expo app got a
    host with no Expo runtime. It now passes whichever the app has installed.
  - ~~**It named no compiler.**~~ CMake took the system default, g++ on Ubuntu,
    and React Native's `-Werror` stopped it in ReactCommon. It names clang now,
    and on Windows clang-cl, a build type and vcpkg's toolchain file.

  On Windows the same app runs too, from `npm run windows -- --build` in a
  plain PowerShell: WSL's `bash.exe` first on PATH, no cmake, no vcvars.
  Getting there took `--build` finding Git Bash and loading the MSVC
  environment itself, and two fixes for expo-modules-core's C++, which had
  never been compiled by anything but clang and GCC: `JSI/ObjectDeallocator.h`
  says `#import`, which clang-cl reads as a COM type-library import, so on
  Windows the build compiles a copy with it spelled `#include`; and
  `TypedArray.cpp` throws `std::runtime_error` without `<stdexcept>`, which
  Microsoft's standard library does not pull in for it. Both are worth
  reporting to Expo.

  What is left before publishing is ordinary: versions, an npm account, the
  publish step, and an install guide. Plus one thing that is not ordinary and
  should happen first: deciding which capabilities ship separately, because
  moving one after the first publish is a breaking change rather than a commit.
  See `openspec/changes/split-optional-capabilities-into-packages`, whose first
  move -- notifications -- is done. And a first `--build` that compiles Hermes
  and React Native's C++ from source, which is the part a user will notice.
- ~~**Adding a desktop to an Expo app is manual.**~~ Done:
  `npx react-native-basalt init` adds this package and the two dev dependencies
  an app needs -- `@react-native/metro-config`, which React Native's `start`
  requires whatever the app's Metro config says, and
  `@react-native-community/cli`, which provides `run-windows` -- wraps the
  Metro config, and adds a script per desktop.

  Idempotent, and it refuses rather than half-configuring: run twice it reports
  what is already right and writes nothing, and run somewhere it cannot
  identify as an app it says what it expected and leaves the directory alone. A
  `metro.config.ts` it cannot safely edit is reported rather than overwritten.

  What is left is the verification the change asked for and this did not do:
  running it against a fresh `create-expo-app`, the way `release.yml`'s install
  job installs the packed packages, and building the result. The unit tests
  cover what it writes; nothing yet covers that what it writes is sufficient.
- **Porting a first third-party native module end to end**, to learn what the
  porting story actually costs. This is the largest unknown in the project: the
  TurboModule seam is proven, by `src/LinuxPlatformConstants.cpp`, but no
  third-party module has been through it, and there is no codegen configuration
  for one. Until a module has been ported, the cost of porting any module is a
  guess.
- Packaging: Arch PKGBUILD, Flatpak.

- **The package ships no types, and an Expo app is TypeScript by default.**
  `create-expo-app` gives you TypeScript, so the first thing a new user writes
  is `import {useWindow} from 'react-native-basalt'` -- and gets nothing.
  `package.json` sets no `types`, there is no `.d.ts` anywhere, and there is no
  `tsconfig.json` in the repository.

  This is drift rather than a decision: nothing in `docs/DECISIONS.md` argues
  for JavaScript, which is where such an argument would live.

  Converting the source is the expensive answer and probably the wrong one --
  `main` points straight at `src/index.js`, so this package has no build step,
  and adding TypeScript to the source adds one. The cheaper shape is JSDoc types
  in the JavaScript, `// @ts-check` in CI, and `.d.ts` generated with
  `tsc --declaration --allowJs --emitDeclarationOnly`: consumers get types, the
  source stays as it is, and nothing gains a runtime build. Worth deciding
  before publishing, because the answer changes the public surface.
