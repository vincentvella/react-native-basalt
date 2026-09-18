# Tasks

## 1. Settle the rule

- [ ] Agree the three tests, or amend them
- [ ] Ensure every capability package reports whether it is supported here, not
      only no-ops
- [ ] Decide which half of spell checking this platform implements
- [ ] Record the outcome in `docs/DECISIONS.md`

## 2. Generalise discovery

- [x] Define the manifest key and the CMake entry point it names
- [ ] Make `optionalNativeModules` scan dependencies for it
- [ ] Keep the Expo, worklets and Reanimated special cases, and say why
- [ ] Fail loudly, naming the package, when a contributed build fails

## 3. Apply it

- [x] Move the notifications seam out of core first -- it is the only shipped
      capability the rule catches, and proving it does not break the
      expo-notifications proxy is what makes the rest safe
- [ ] Move what the rule moves, in one commit per package
- [ ] Have `init` install what an app needs so the split is invisible to it
- [ ] Update the specs whose capability moved
