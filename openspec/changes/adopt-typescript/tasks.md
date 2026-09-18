# Tasks

## 1. The build, before anything uses it

- [x] `tsconfig.json` with `strict: true`, emitting CommonJS
- [x] A build script, and `prepack` so a packed package is always built
- [ ] CI type-checks and builds, on a job that already runs
- [x] Prove the scaffolding with nothing converted yet -- `allowJs`, so the
      package builds and works before a single file is TypeScript

## 2. The public surface

- [x] `src/*.js` to TypeScript, excluding `src/overrides/` -- all nine files
- [x] `package.json`: `main`, `types`, `bin`, `exports` and `files` all point
      at `dist`; `react-native.config.js` stays at the root, which is where
      React Native's CLI looks for it by convention
- [x] `exports` resolves every current specifier to the same shape it does today
- [x] A consumer type-checks against the package, and three wrong calls fail

## 3. The CLI and the config files

- [ ] `cli/*.js`, and land `cli/init.js` as TypeScript
- [ ] `metro-config.js` and `react-native.config.js`, emitted as CommonJS at the
      paths tools outside this project require them from
- [ ] The `bin` entries still run under `npx`

## 4. The overrides

- [ ] `src/overrides/*.js`, last and most carefully
- [x] `OVERRIDE_DIR` resolves correctly -- the overrides are *copied* into
      `dist`, not compiled; see the tsconfig for why
- [x] `Platform.linux.js`, `Platform.macos.js` and `Platform.windows.js` keep
      those exact names, by being copied rather than compiled
- [x] Bundle a demo app and assert `Platform.OS` -- done, reports `linux`

## 5. Everything that bundles must build first

- [x] `scripts/bundle.sh` builds first
- [ ] `compare_all.sh` and the CI jobs
- [ ] A clear failure when the build has not run, rather than a confusing one

## 6. Records

- [ ] `docs/ARCHITECTURE.md`: the build step and why `main` moved
- [ ] Strike the entry in `docs/backlog/ecosystem.md`
