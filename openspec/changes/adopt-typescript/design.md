# Design

## What must not move

Three things are read by tools this project does not control, out of an app's
`node_modules`, as CommonJS:

- `react-native.config.js` -- React Native's CLI, to believe these are platforms
- `metro-config.js` -- the app's own `metro.config.js` requires it by that path
- the `bin` entries -- `npx` resolves them from `package.json`

They can be authored in TypeScript, but they must be *emitted* as CommonJS at
exactly those paths. The `exports` map already names them, so the map is the
contract: whatever the layout becomes, those specifiers resolve to the same
shapes.

## The overrides are copied, not compiled -- which removes the hazard

*Revised once the build existed, which is why step 1 was to build nothing.*

Two things were found by running `tsc` rather than by reading:

- **Three of the thirteen overrides are Flow, not JavaScript.** `Alert.js`,
  `Share.js` and `setUpDeveloperTools.js` carry optional-parameter and type-alias
  syntax inherited from the React Native files they rewrite. `tsc` cannot even
  copy them through.
- **They never reach node.** Metro resolves them by absolute path out of
  `OVERRIDE_DIR`, and by platform extension for `Platform.linux.js` and its
  siblings, then runs them through React Native's own Babel -- which strips Flow
  from everything.

So they are copied into `dist` verbatim by the build script. That keeps their
filenames by construction, keeps them in the dialect of the upstream files they
shadow so the two stay diffable, and turns the hazard below into a non-issue.

## The override paths were the hazard

`PLATFORM_OVERRIDES` does not return module specifiers. It returns absolute file
paths ending in `.js`, and Metro's `resolveRequest` uses them directly:

    path.join(OVERRIDE_DIR, 'BaseViewConfig.js')
    platform => path.join(OVERRIDE_DIR, `Platform.${platform}.js`)

So `OVERRIDE_DIR` has to point at compiled output, and compilation has to
preserve those filenames -- including the platform extensions, which are
`Platform.linux.js`, `Platform.macos.js`, `Platform.windows.js` and are selected
by that name rather than by anything in the module system.

This is the failure that would not look like a type error. It looks like an app
bundling with React Native's own `Platform` instead of this one, and the symptom
is `Platform.OS` being `android` a long way from here.

## An unbuilt checkout stops working

Today `main` points at `src/index.js` and a fresh clone can bundle immediately.
After this it cannot, until `tsc` has run. Everything that bundles has to build
first: `scripts/bundle.sh`, `scripts/integration_test.py`, `compare_all.sh`, the
CI jobs, and the demo apps in `js/`.

Making the build cheap matters more than making it clever. One `tsc -b`, no
bundler, no transform beyond what TypeScript does.

## Strictness

`strict: true` from the start, because retrofitting it is the expensive version
of this work and there are only 5,000 lines to argue with. Where a React Native
internal has no usable type -- and the overrides are full of them, since they
shadow files React Native does not export types for -- the honest answer is a
narrow local type with a comment saying what it approximates, not `any` spread
outward.

## Order

The overrides last. They are the files most likely to need an escape hatch, and
doing them first would set the strictness bar by the worst case.

1. `tsconfig`, the build, and CI running it -- with no files converted, so the
   scaffolding is proven before it carries anything.
2. The public surface, `src/*.ts`, which is what consumers see.
3. The CLI and the config files, where the emitted paths matter.
4. The overrides.
