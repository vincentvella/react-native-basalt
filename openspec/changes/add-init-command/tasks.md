# Tasks

## 1. The command

- [ ] `cli/init.js`, registered in `react-native.config.js` beside the run commands
- [ ] Detect the app: `package.json`, an Expo or React Native dependency, a Metro config
- [ ] Add `@react-native/metro-config` and `@react-native-community/cli` as dev dependencies
- [ ] Wrap the Metro config in `withDesktopPlatforms`, preserving whatever is already there
- [ ] Add a script per supported desktop

## 2. Idempotence and refusal

- [ ] Re-running reports each step as already done and writes nothing
- [ ] An unrecognised directory is refused, naming what was expected
- [ ] A Metro config the command cannot safely edit is reported rather than overwritten

## 3. Verification

- [ ] Run against a fresh `create-expo-app`, following `release.yml`'s install job
- [ ] Bundle and build the result for one desktop, to prove the configuration is complete
- [ ] Replace the README's manual instructions with the command

## 4. Records

- [ ] Strike the entry in `docs/backlog/ecosystem.md`
- [ ] Update the README's "Next"
