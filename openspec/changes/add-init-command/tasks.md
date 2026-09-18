# Tasks

## 1. The command

- [x] `cli/init.ts`, as the package's `react-native-basalt` bin -- not a React
      Native CLI command, because before it runs the package is not a
      dependency yet and the CLI has no config to read
- [x] Detect the app: `package.json`, an Expo or React Native dependency, a Metro config
- [x] Add `@react-native/metro-config` and `@react-native-community/cli` as dev dependencies
- [x] Wrap the Metro config in `withDesktopPlatforms`, preserving whatever is already there
- [x] Add a script per supported desktop

## 2. Idempotence and refusal

- [x] Re-running reports each step as already done and writes nothing
- [x] An unrecognised directory is refused, naming what was expected
- [x] A Metro config the command cannot safely edit is reported rather than overwritten

## 3. Verification

- [ ] Run against a fresh `create-expo-app`, following `release.yml`'s install job
- [ ] Bundle and build the result for one desktop, to prove the configuration is complete
- [x] Replace the README's manual instructions with the command

## 4. Records

- [x] Strike the entry in `docs/backlog/ecosystem.md`
- [x] Update the README's "Next"
