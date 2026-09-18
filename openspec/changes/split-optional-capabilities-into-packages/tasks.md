# Tasks

## 1. Settle the rule

- [ ] Agree the three tests, or amend them
- [ ] Answer the open question on menus, dialogs and the title bar
- [ ] Answer the open question on single-desktop capabilities
- [ ] Record the outcome in `plan/decisions.md`

## 2. Generalise discovery

- [ ] Define the manifest key and the CMake entry point it names
- [ ] Make `optionalNativeModules` scan dependencies for it
- [ ] Keep the Expo, worklets and Reanimated special cases, and say why
- [ ] Fail loudly, naming the package, when a contributed build fails

## 3. Apply it

- [ ] Move what the rule moves, in one commit per package
- [ ] Have `init` install what an app needs so the split is invisible to it
- [ ] Update the specs whose capability moved
